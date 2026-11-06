// Shorts tab: full-screen vertical pager that plays the short in view.

#include "app/App.hpp"
#include "app/ui/Motion.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"
#include "app/ui/Touch.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <span>

namespace sloptube::app {
namespace {

using innertube::Rating;
using innertube::toggled;

// s between live seeks while dragging the bar: each must reach a frame before the next.
constexpr double kScrubSeekInterval = 0.4;
constexpr float kPageThreshold = 0.18f;   // Fraction of a page a drag must travel to turn it.
constexpr float kFlingSpeed = 600;        // dp/s that turns a page whatever the distance.
constexpr float kSnapRate = 12;           // Exponential rate of the snap animation.
constexpr std::size_t kPrefetchAhead = 3; // Load more when this close to the end.
// Right-hand action column, measured on the official app: 60 dp between buttons, the label
// centred 22 dp below the icon's centre, the column's axis 32 dp from the right edge. From its
// APK: each cell is 64 dp wide (reel_player_right_cell_width) and its icon a 32 dp drawable
// (youtube_shorts_like_outline_32dp and its siblings), labelled in 12 sp medium (body S heavy).
constexpr float kActionSpacing = 60; // dp
constexpr float kActionIcon = 32;    // dp
constexpr float kActionLabel = 22;   // dp from the icon's centre to the label's centre.
constexpr float kActionAxis = 32;    // dp from the right edge.
constexpr float kActionWidth = 64;   // dp, width of a button's touch target.
// Pressed, a cell shows a 64 dp circle of white at 10 % (reel_player_dyn_cell_background_pressed)
// and its icon shrinks to 90 % (shorts_button_reduce_size_animation).
constexpr ImU32 kActionPressed = IM_COL32(0xff, 0xff, 0xff, 0x1a);
constexpr float kActionPressScale = 0.9f;
// Glyphs and text over the video are pure white (yt_white1) with a shadow of black at 40 %
// (reel_player_right_cell_shadow_color) 1 dp below (shadowDx 0, shadowDy 1).
constexpr ImU32 kOverlayText = IM_COL32_WHITE;
constexpr ImU32 kShadow = IM_COL32(0, 0, 0, 0x66);
constexpr float kShadowOffset = 1; // dp
// Channel row and title, from the APK's reel_player_right_footer: 16 dp from the left edge
// (reel_player_right_container_margin), the title stopping 80 dp from the right
// (reel_title_margin_end) in 16 sp regular (reel_primary_font_size, body L), the channel name in
// 14 sp medium (reel_secondary_font_size) 8 dp after the avatar and 8 dp before Subscribe
// (reel_player_right_byline_margin_inner).
constexpr float kTextStart = 16;     // dp
constexpr float kTextEnd = 80;       // dp
constexpr float kTitleSize = 16;     // sp
constexpr float kBylineGap = 8;      // dp
constexpr float kChannelAvatar = 32; // dp
// Scrims (reel_player_overlay_scrims, vertical): black at 30 % (reel_black_pure_alpha_30) over
// the bottom 64 dp, fading out over the 120 dp above, and from the top over 64 dp.
constexpr ImU32 kReelScrim = IM_COL32(0, 0, 0, 0x4c);
constexpr float kScrimBottomSolid = 64; // dp
constexpr float kScrimBottomFade = 120; // dp
constexpr float kScrimTopFade = 64;     // dp
constexpr float kSheetTop = 0.3f;       // Fraction of the page above the comments and description.
constexpr float kProgressHeight = 2;    // dp, reel_progress_bar_height.
constexpr int kPosterWidth = 720;       // px
constexpr int kShortTitleLines = 2;

// Keyframed animations measured from the official app's Lottie files: values at frame numbers,
// eased in and out between them.
struct Key {
    float frame;
    float value;
};

[[nodiscard]] float keyed(std::span<const Key> keys, float frame) {
    if (frame <= keys.front().frame) {
        return keys.front().value;
    }
    for (std::size_t i = 1; i < keys.size(); ++i) {
        const Key& a = keys[i - 1];
        const Key& b = keys[i];
        if (frame <= b.frame) {
            const float t = (frame - a.frame) / (b.frame - a.frame);
            return a.value + (b.value - a.value) * motion::easeInOutCubic(t);
        }
    }
    return keys.back().value;
}

// Turns what was drawn since vertex `first` by `degrees` about `pivot`.
void rotateSince(ImDrawList* list, int first, ImVec2 pivot, float degrees) {
    const float radians = degrees * std::numbers::pi_v<float> / 180;
    const float cos = std::cos(radians);
    const float sin = std::sin(radians);
    for (int i = first; i < list->VtxBuffer.Size; ++i) {
        ImVec2& pos = list->VtxBuffer[i].pos;
        const ImVec2 d{pos.x - pivot.x, pos.y - pivot.y};
        pos = {pivot.x + d.x * cos - d.y * sin, pivot.y + d.x * sin + d.y * cos};
    }
}

// The red and pink of the official like animations (#ff0033 is the accent; #ff2691).
constexpr ImU32 kLikePink = IM_COL32(0xff, 0x26, 0x91, 0xff);

// Double-tap like, after like_tap_feedback_shorts_heart (60 fps, a 290-unit composition shown
// 240 dp wide, reel_player_tap_feedback_like_width): a heart filled red to pink fades in above
// the tap, grows to 125 % while rising, wobbles, then sinks back and fades out. The heart is
// SlopTube's own shape at the official one's size, 67 x 60 units at 100 %.
constexpr float kHeartFps = 60;
constexpr float kHeartEnd = 73;               // Frames.
constexpr float kHeartUnit = 240.0f / 290.0f; // dp per unit.
constexpr ImVec2 kHeartSize{67.25f, 60.1f};   // Units.
constexpr std::array kHeartOpacity{Key{2, 0}, Key{4, 0.9f}, Key{14, 1}, Key{64, 1}, Key{72, 0}};
constexpr std::array kHeartScale{Key{2, 0.958f}, Key{4, 1},      Key{14, 1.25f}, Key{27, 1.22f},
                                 Key{36, 1.25f}, Key{64, 1.25f}, Key{72, 0.958f}};
// Height of the heart's centre above the tap, units.
constexpr std::array kHeartRise{Key{2, 11.3f},  Key{4, 10.4f},  Key{14, 27.2f}, Key{27, 24},
                                Key{36, 27.2f}, Key{64, 22.2f}, Key{72, 11.4f}};
// The gradient runs from the bottom left to the top right: red up to 60 % of the way, pink from
// 85 %. Ends in units from the heart's centre at 100 %.
constexpr ImVec2 kHeartRed{-1.0f, -13.2f};
constexpr ImVec2 kHeartPinkEnd{19.5f, -31.2f};

// The like button turning on, after reel_animated_like_icon_v3 (60 fps over 60 frames, a 48-unit
// composition). Its final thumb is 19 units wide where the static filled icon is 25.3 dp, so the
// composition is drawn 64 dp wide, which also fits topic animations to the column.
constexpr float kLikeFps = 60;
constexpr float kLikeEnd = 60;                     // Frames.
constexpr float kLikeComposition = 64;             // dp
constexpr float kLikeUnit = kLikeComposition / 48; // dp per unit.
constexpr int kLikeAnimationFrames = 30;           // Topic animations are kept at 30 fps.
// Frames 0-4: the outline shrinks to 30 % and turns 6°; then the filled thumb pops from 50 % to
// 120 %, wobbling from 15° to -10° and 5°, and rises 2 units on the way.
constexpr float kLikeSwap = 4;
constexpr std::array kLikeOutlineScale{Key{0, 1}, Key{4, 0.3f}};
constexpr std::array kLikeOutlineTurn{Key{0, 0}, Key{4, 6}};
constexpr std::array kLikeScale{Key{4, 0.5f}, Key{17, 1.2f}, Key{37, 1}};
constexpr std::array kLikeTurn{Key{4, 15}, Key{17, -10}, Key{37, 5}, Key{60, 0}};
constexpr std::array kLikeRise{Key{4, 0}, Key{17, 2}, Key{37, 0}};
// Ten rays, five red and five pink between them, shoot out from frame 8 to 36 while their centre
// drops 4 units: radii in units, widths thinning from frame 12.
constexpr float kRaysStart = 8;
constexpr float kRaysEnd = 36;
constexpr std::size_t kRays = 5;
constexpr std::array kRaysDrop{Key{12, 0}, Key{36, 4}};

struct Ray {
    ImU32 color;
    float angle; // Degrees from straight up, for the first one.
    std::array<Key, 4> inner;
    std::array<Key, 4> outer;
    std::array<Key, 2> width;
    std::array<Key, 2> turn;
};

constexpr std::array kLikeRays{
    Ray{theme::kAccent,
        0,
        {Key{0, 9.1f}, Key{4, 13.3f}, Key{8, 15.3f}, Key{28, 19.9f}},
        {Key{0, 12.9f}, Key{4, 18.1f}, Key{8, 19.9f}, Key{28, 20}},
        {Key{12, 3}, Key{28, 0}},
        {Key{0, 0}, Key{28, -6}}},
    Ray{kLikePink,
        36,
        {Key{0, 8.1f}, Key{4, 12.3f}, Key{8, 14.3f}, Key{28, 18.9f}},
        {Key{0, 11.9f}, Key{4, 17.1f}, Key{8, 18.9f}, Key{28, 19}},
        {Key{12, 2}, Key{28, 0}},
        {Key{0, 0}, Key{32, 6}}},
};

// Tapping to pause or play, after pause_tap_feedback and play_tap_feedback (30 fps, 96 dp): the
// glyph in a 72 dp circle of black at 60 % pops to 130 % (the circle to 110 %), settles, and fades
// out (frames 17-22 for play, 25-30 for pause). Where the pause glyph would fade, SlopTube shows
// the play button at rest instead, kept while paused so the state stays visible.
constexpr float kFeedbackFps = 30;
constexpr float kFeedbackCircle = 72; // dp
constexpr ImU32 kFeedbackFill = IM_COL32(0, 0, 0, 0x99);
constexpr std::array kFeedbackGlyphScale{Key{0, 0}, Key{4, 1.3f}, Key{13, 1}};
constexpr std::array kFeedbackCircleScale{Key{0, 0}, Key{4, 1.1f}, Key{13, 1}};
constexpr std::array kPlayFade{Key{17, 1}, Key{22, 0}};
constexpr float kPauseHold = 25;      // Frames.
constexpr ImVec2 kPlayGlyph{19, 20};  // dp: a triangle pointing right.
constexpr ImVec2 kPauseBar{4, 18};    // dp: two bars, 10 dp between their centres.
constexpr float kPauseBarsApart = 10; // dp
// Shorts around the current one whose responses and posters are fetched ahead; the next one is
// also preloaded on the player's standby session.
constexpr std::size_t kPrefetchNext = 2;
constexpr std::size_t kPrefetchPrevious = 1;
constexpr std::size_t kPrefetchedShorts = 4; // Responses kept, oldest dropped first.
// Largest zoom a short is cropped by to fill the page, as in the official app: a 9:16 video on a
// 20:9 screen needs about 1.1.
constexpr float kShortFillLimit = 1.3f;
constexpr ImU32 kScrimBottom = IM_COL32(0, 0, 0, 0xb0);
constexpr ImU32 kTransparent = IM_COL32(0, 0, 0, 0);

// Home feed shelves, measured on the official app: a 2-column grid of 2:3 tiles.
constexpr std::size_t kShelfColumns = 2;
constexpr std::size_t kShelfRows = 3;
constexpr std::size_t kShelfShorts = kShelfColumns * kShelfRows;
constexpr std::size_t kFirstShelfAfter = 1; // Videos above the first shelf.
constexpr std::size_t kShelfEvery = 12;     // Videos between two shelves.
constexpr float kShelfHeader = 64;          // dp
constexpr float kShelfGutter = 12;          // dp
constexpr float kShelfGap = 8;              // dp
constexpr float kShelfTileAspect = 1.5f;    // Height over width.
constexpr float kShelfTextInset = 8;        // dp
constexpr float kShelfScrim = 0.35f;        // Fraction of a tile darkened behind its title.

std::string posterUrl(const innertube::Item& item) {
    return item.thumbnail.empty() ? imageUrl(item) : item.thumbnail;
}

} // namespace

void App::playShort(const innertube::Item& item) {
    closeVideo();
    m_watch = WatchState{.item = item};
    m_shorts.playing = item.id;

    auto& prefetched = m_shorts.prefetched;
    const auto found = std::ranges::find(prefetched, item.id, &ShortsState::Prefetched::id);
    std::optional<innertube::Watch> watch;
    std::optional<innertube::Player> player;
    if (found != prefetched.end()) {
        watch = std::move(found->watch);
        player = std::move(found->player);
        prefetched.erase(found);
    }
    if (watch) {
        applyWatch(item.id, std::move(*watch));
    } else {
        m_tasks.run([this, id = item.id] { (void)fetchWatch(id); });
    }
    if (m_player.promote(item.id)) {
        m_watch.details = *m_player.details();
    } else if (player) {
        beginPlayback(std::move(*player));
    } else {
        startPlayback(item.id);
    }
    m_shorts.standby.clear();
}

void App::prefetchShorts(std::span<const innertube::Item> items, std::size_t index) {
    auto& prefetched = m_shorts.prefetched;
    const auto entry = [&](const std::string& id) {
        return std::ranges::find(prefetched, id, &ShortsState::Prefetched::id);
    };
    const std::size_t first = index - std::min(index, kPrefetchPrevious);
    const std::size_t last = std::min(items.size() - 1, index + kPrefetchNext);
    for (std::size_t i = first; i <= last; ++i) {
        const auto& item = items[i];
        (void)m_thumbnails->get(posterUrl(item), kPosterWidth); // Starts the download.
        if (i == index || item.id == m_shorts.playing || entry(item.id) != prefetched.end()) {
            continue;
        }
        if (prefetched.size() >= kPrefetchedShorts) {
            prefetched.erase(prefetched.begin());
        }
        prefetched.push_back({.id = item.id, .watch = {}, .player = {}});
        m_tasks.run([this, id = item.id] {
            auto watch = m_api.watch(id);
            auto player = m_api.player(id);
            m_tasks.post(
                [this, id, watch = std::move(watch), player = std::move(player)]() mutable {
                    auto& entries = m_shorts.prefetched;
                    const auto found = std::ranges::find(entries, id, &ShortsState::Prefetched::id);
                    if (found == entries.end()) {
                        return;
                    }
                    if (watch) {
                        found->watch = std::move(*watch);
                    }
                    if (player) {
                        found->player = std::move(*player);
                    }
                });
        });
    }

    // The next short is opened once the current one plays, so that it does not compete with it.
    if (index + 1 >= items.size() || m_player.state() != media::PlayerState::Playing) {
        return;
    }
    const std::string& next = items[index + 1].id;
    const auto ready = entry(next);
    if (m_shorts.standby != next && ready != prefetched.end() && ready->player) {
        m_shorts.standby = next;
        m_player.preload(*ready->player, screenLines());
    }
}

void App::openShort(std::size_t index) {
    selectTab(Tab::Shorts);
    m_shorts.index = index;
    m_shorts.position = static_cast<float>(index);
}

std::vector<ListInsert> App::shortsShelves(const Canvas& c, float width) {
    std::vector<ListInsert> shelves;
    const float height = shortsShelfHeight(c, width);
    std::size_t after = kFirstShelfAfter;
    for (std::size_t first = 0; first + kShelfShorts <= shortsFeed().page.items.size();
         first += kShelfShorts, after += kShelfEvery) {
        shelves.push_back({
            .after = after,
            .height = height,
            .draw = [this, first](Frame& f, ImVec2 origin,
                                  float w) { drawShortsShelf(f, origin, w, first); },
        });
    }
    return shelves;
}

float App::shortsShelfHeight(const Canvas& c, float width) const {
    const float tile = (width - c.dp(kShelfGutter * 2 + kShelfGap)) / kShelfColumns;
    return c.dp(kShelfHeader + theme::kCardSpacing) + tile * kShelfTileAspect * kShelfRows +
           c.dp(kShelfGap) * (kShelfRows - 1);
}

void App::drawShortsShelf(Frame& frame, ImVec2 origin, float width, std::size_t first) {
    const Canvas& c = frame.canvas;
    const float gutter = c.dp(kShelfGutter);
    const float gap = c.dp(kShelfGap);
    const float tile = (width - gutter * 2 - gap) / kShelfColumns;
    const float tileHeight = tile * kShelfTileAspect;

    // Header: the Shorts icon and the title.
    const float centerY = origin.y + c.dp(kShelfHeader) * 0.5f;
    const float icon = c.dp(theme::kIconSize);
    c.icon(Icon::Shorts, {origin.x + gutter + icon * 0.5f, centerY}, theme::kIconSize,
           theme::g_textPrimary);
    c.with(theme::kHeadingWeight)
        .text(
            {origin.x + gutter * 1.75f + icon, centerY - c.lineHeight(theme::kSectionSize) * 0.5f},
            theme::kSectionSize, theme::g_textPrimary, tr(Text::Shorts));

    const auto& items = shortsFeed().page.items;
    const float top = origin.y + c.dp(kShelfHeader);
    for (std::size_t i = 0; i < kShelfShorts && first + i < items.size(); ++i) {
        const auto& item = items[first + i];
        const float x = origin.x + gutter + static_cast<float>(i % kShelfColumns) * (tile + gap);
        const float y = top + static_cast<float>(i / kShelfColumns) * (tileHeight + gap);
        const ImVec2 min{x, y};
        const ImVec2 max{x + tile, y + tileHeight};
        drawThumbnail(frame, item.thumbnail.empty() ? imageUrl(item) : item.thumbnail, min, max,
                      theme::kCornerRadius);
        c.list()->AddRectFilledMultiColor({x, max.y - tileHeight * kShelfScrim}, max, kTransparent,
                                          kTransparent, kScrimBottom, kScrimBottom);
        drawPressed(frame, min, max, theme::kCornerRadius);
        const float inset = c.dp(kShelfTextInset);
        const float titleHeight = c.lineHeight(theme::kBodySize) * 2;
        c.with(theme::kTitleWeight)
            .paragraph({x + inset, max.y - inset - titleHeight}, tile - inset * 2, theme::kBodySize,
                       IM_COL32_WHITE, item.title, 2);
        // Hit box clamped to the list so tiles scrolled under the bars leave them their taps.
        const ImVec2 clipMin = c.list()->GetClipRectMin();
        const ImVec2 clipMax = c.list()->GetClipRectMax();
        if (frame.touch.tapped({x, std::max(y, clipMin.y)}, {max.x, std::min(max.y, clipMax.y)})) {
            later([this, index = first + i] { openShort(index); });
        }
    }
}

void App::drawShorts(Frame& frame, ImVec2 min, ImVec2 max) {
    const theme::DarkScope darkColours;
    const Canvas& c = frame.canvas;
    const auto& feed = m_tabFeeds[std::to_underlying(Tab::Shorts)];
    c.rect(min, max, IM_COL32_BLACK);
    auto& items = feed->page.items;
    if (items.empty()) {
        if (drawFeedStatus(frame, *feed, {min.x, min.y + m_insets.top}, max) && feed->request) {
            load(feed, feed->request);
        }
        return;
    }

    ImDrawList* list = c.list();
    const bool sheet =
        (m_sheet.kind == Sheet::Comments || m_sheet.kind == Sheet::Description) && m_watch.info;
    const float page = max.y - min.y;
    const ImVec2 sheetMin{min.x, min.y + page * kSheetTop};
    const bool blocked = m_touch.blocked();
    if (sheet) {
        list->ChannelsSplit(2);
        list->ChannelsSetCurrent(1);
        drawSheet(frame, sheetMin, max);
        list->ChannelsSetCurrent(0);
        if (frame.touch.startedIn(sheetMin, max)) {
            m_touch.setBlocked(true);
        }
    }

    // Seeking: a drag along the bottom edge moves through the short itself, live, as in the
    // official app; it never pages.
    auto& state = m_shorts;
    const float duration = static_cast<float>(m_player.durationUs());
    const ImVec2 scrubMin{min.x, max.y - c.dp(theme::kScrubZone)};
    // Also true on the frame the seek ends, so its release is not taken for a page fling.
    const bool scrubbing =
        state.scrub.has_value() ||
        (!sheet && duration > 0 && frame.touch.dragging() && frame.touch.startedIn(scrubMin, max));
    if (!sheet && duration > 0 && frame.touch.dragging() && frame.touch.startedIn(scrubMin, max)) {
        if (!state.scrub) {
            state.scrubFrom =
                std::clamp(static_cast<float>(m_player.positionUs()) / duration, 0.0f, 1.0f);
            state.undo = {};
        }
        state.scrub = std::clamp((frame.touch.position().x - min.x) / (max.x - min.x), 0.0f, 1.0f);
        if (state.undo.update(std::abs(*state.scrub - state.scrubFrom) * (max.x - min.x) / c.dp(1),
                              frame.time)) {
            m_platform.haptic(Haptic::Click);
        }
        if (frame.time - state.seekedAt > kScrubSeekInterval && *state.scrub != state.seekedTo) {
            m_player.seek(static_cast<std::int64_t>(*state.scrub * duration));
            state.seekedAt = frame.time;
            state.seekedTo = *state.scrub;
        }
        frame.animating = true;
    } else if (state.scrub) {
        // Seeks are live, so undoing one goes back to where playback was.
        const float to = state.undo.armed(frame.time) ? state.scrubFrom : *state.scrub;
        if (to != state.seekedTo) {
            m_player.seek(static_cast<std::int64_t>(to * duration));
        }
        state.scrub.reset();
        state.seekedTo = -1;
    }

    // Paging: follow the finger, then snap to the nearest page (or the next one on a fling).
    state.index = std::min(state.index, items.size() - 1);
    float target = static_cast<float>(state.index);
    if (!sheet && !scrubbing && frame.touch.dragging() && frame.touch.startedIn(min, max)) {
        target -= (frame.touch.position().y - frame.touch.start().y) / page;
        state.position = target;
    } else if (const auto fling = frame.touch.flingVelocity();
               fling && !sheet && !scrubbing && frame.touch.startedIn(min, max)) {
        const float travel = (frame.touch.position().y - frame.touch.start().y) / page;
        const bool forward = travel < -kPageThreshold || fling->y < -c.dp(kFlingSpeed);
        const bool backward = travel > kPageThreshold || fling->y > c.dp(kFlingSpeed);
        if (forward && state.index + 1 < items.size()) {
            ++state.index;
        } else if (backward && state.index > 0) {
            --state.index;
        }
        target = static_cast<float>(state.index);
    } else if (motion::approach(state.position, target, frame.seconds, kSnapRate)) {
        frame.animating = true;
    }
    state.position = std::clamp(state.position, -0.3f, static_cast<float>(items.size()) - 0.7f);

    if (items[state.index].id != state.playing) {
        later([this, item = items[state.index]] { playShort(item); });
    } else {
        prefetchShorts(items, state.index);
        // Only a signed-in account can like, and the request is large: asked for the short being
        // watched, once it plays so that it does not compete with its start.
        if (m_session.signedIn() && m_player.state() == media::PlayerState::Playing) {
            loadLikeAnimation(state.playing);
        }
    }
    if (!feed->loading && !feed->page.continuation.empty() &&
        state.index + kPrefetchAhead >= items.size()) {
        loadMore(feed);
    }

    const auto first = static_cast<std::size_t>(std::max(0.0f, std::floor(state.position)));
    for (std::size_t i = first; i < items.size() && i <= first + 1; ++i) {
        const float top = min.y + (static_cast<float>(i) - state.position) * page;
        if (top >= max.y || top + page <= min.y) {
            continue;
        }
        list->PushClipRect({min.x, std::max(top, min.y)}, {max.x, std::min(top + page, max.y)},
                           true);
        drawShort(frame, items[i], i == state.index, {min.x, top}, {max.x, top + page});
        list->PopClipRect();
    }

    m_touch.setBlocked(blocked);
    if (sheet) {
        list->ChannelsMerge();
    }
}

void App::drawLikeBurst(Frame& frame) {
    const float heartFrame = static_cast<float>(frame.time - m_shorts.burstAt) * kHeartFps;
    if (m_shorts.burstAt < 0 || heartFrame > kHeartEnd) {
        m_shorts.burstAt = -1;
        return;
    }
    frame.animating = true;
    const Canvas& c = frame.canvas;
    const float alpha = keyed(kHeartOpacity, heartFrame);
    if (alpha <= 0) {
        return;
    }
    // Composition units to pixels at the current scale.
    const float unit = c.dp(kHeartUnit) * keyed(kHeartScale, heartFrame);
    const ImVec2 centre{m_shorts.burstCentre.x,
                        m_shorts.burstCentre.y - c.dp(kHeartUnit) * keyed(kHeartRise, heartFrame)};
    const float w = kHeartSize.x * unit;
    const float h = kHeartSize.y * unit;
    const auto p = [&](float x, float y) { return ImVec2{centre.x + x * w, centre.y + y * h}; };

    // Two lobes meeting in a notch, curving down to the point.
    ImDrawList* list = c.list();
    const int first = list->VtxBuffer.Size;
    list->PathLineTo(p(0, -0.28f));
    list->PathBezierCubicCurveTo(p(-0.12f, -0.52f), p(-0.5f, -0.56f), p(-0.5f, -0.16f));
    list->PathBezierCubicCurveTo(p(-0.5f, 0.12f), p(-0.2f, 0.3f), p(0, 0.5f));
    list->PathBezierCubicCurveTo(p(0.2f, 0.3f), p(0.5f, 0.12f), p(0.5f, -0.16f));
    list->PathBezierCubicCurveTo(p(0.5f, -0.56f), p(0.12f, -0.52f), p(0, -0.28f));
    list->PathFillConcave(motion::withAlpha(IM_COL32_WHITE, alpha));
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(
        list, first, list->VtxBuffer.Size,
        {centre.x + kHeartRed.x * unit, centre.y + kHeartRed.y * unit},
        {centre.x + kHeartPinkEnd.x * unit, centre.y + kHeartPinkEnd.y * unit}, theme::kAccent,
        kLikePink);
}

void App::drawLikeIcon(Frame& frame, bool liked, ImVec2 center, float scale) {
    const Canvas& c = frame.canvas;
    const auto& sheet = m_shorts.likeAnimation;
    const double started = m_watch.likePopAt;
    // A like set before the short was shown starts at its end.
    const float elapsed = liked && started >= 0 ? static_cast<float>(frame.time - started) : 1e9f;
    const ImVec2 shadowAt{center.x, center.y + c.dp(kShadowOffset)};
    if (!liked) {
        c.icon(Icon::ReelLike, shadowAt, kActionIcon * scale, kShadow);
        c.icon(Icon::ReelLike, center, kActionIcon * scale, kOverlayText);
        return;
    }
    if (sheet.ready()) {
        // The short's own animation, which carries its shadow; its last frame is the liked icon.
        const float progress = sheet.duration() > 0 ? elapsed / sheet.duration() : 1;
        if (progress < 1) {
            frame.animating = true;
        }
        sheet.draw(c.list(), center, c.dp(kLikeComposition) * scale, progress);
        return;
    }

    const float at = elapsed * kLikeFps;
    if (at < kLikeEnd) {
        frame.animating = true;
    }
    ImDrawList* list = c.list();
    if (at < kLikeSwap) {
        const int first = list->VtxBuffer.Size;
        c.icon(Icon::ReelLike, center, kActionIcon * scale * keyed(kLikeOutlineScale, at),
               kOverlayText);
        rotateSince(list, first, center, keyed(kLikeOutlineTurn, at));
        return;
    }

    if (at >= kRaysStart && at < kRaysEnd) {
        const float local = at - kRaysStart;
        const float unit = c.dp(kLikeUnit) * scale;
        const ImVec2 origin{center.x, center.y + keyed(kRaysDrop, at) * unit};
        for (const Ray& ray : kLikeRays) {
            const float width = keyed(ray.width, local) * unit;
            const float inner = keyed(ray.inner, local) * unit;
            const float outer = keyed(ray.outer, local) * unit;
            if (width <= 0 || outer <= inner) {
                continue;
            }
            for (std::size_t i = 0; i < kRays; ++i) {
                const float degrees = ray.angle + keyed(ray.turn, local) +
                                      static_cast<float>(i) * 360 / static_cast<float>(kRays);
                const float radians = (degrees - 90) * std::numbers::pi_v<float> / 180;
                const ImVec2 d{std::cos(radians), std::sin(radians)};
                list->AddLine({origin.x + d.x * inner, origin.y + d.y * inner},
                              {origin.x + d.x * outer, origin.y + d.y * outer}, ray.color, width);
            }
        }
    }
    const ImVec2 at2{center.x, center.y - keyed(kLikeRise, at) * c.dp(kLikeUnit) * scale};
    const float size = kActionIcon * scale * keyed(kLikeScale, at);
    const int first = list->VtxBuffer.Size;
    c.icon(Icon::ReelLikeFilled, {at2.x, at2.y + c.dp(kShadowOffset)}, size, kShadow);
    c.icon(Icon::ReelLikeFilled, at2, size, kOverlayText);
    rotateSince(list, first, at2, keyed(kLikeTurn, at));
}

void App::loadLikeAnimation(const std::string& videoId) {
    if (m_shorts.likeAnimationFor == videoId) {
        return;
    }
    m_shorts.likeAnimationFor = videoId;
    const int cell = static_cast<int>(std::lround(kLikeComposition * m_density));
    m_tasks.run([this, videoId, cell] {
        auto url = m_api.likeAnimation(videoId);
        // On failure the thumb's own animation plays.
        m_tasks.post([this, videoId, cell, url = url ? std::move(*url) : std::string()] {
            auto& shorts = m_shorts;
            if (shorts.likeAnimationFor != videoId || shorts.likeAnimationUrl == url) {
                return; // Moved on, or the same topic as the short before: already rendered.
            }
            shorts.likeAnimation.reset();
            shorts.likeAnimationUrl = url;
            if (url.empty()) {
                return;
            }
            m_tasks.run([this, url, cell] {
                auto response = m_http.send({.url = url, .retryOnStall = true});
                if (!response || !response->ok()) {
                    return;
                }
                auto frames = renderLottie(response->body, cell, kLikeAnimationFrames);
                if (!frames) {
                    return;
                }
                m_tasks.post(
                    [this, url, frames = std::make_shared<LottieFrames>(std::move(*frames))] {
                        if (m_shorts.likeAnimationUrl == url) {
                            m_shorts.likeAnimation.upload(*frames);
                        }
                    });
            });
        });
    });
}

void App::drawPlayFeedback(Frame& frame, ImVec2 center, bool paused) {
    const Canvas& c = frame.canvas;
    const float at = m_shorts.toggledAt >= 0
                         ? static_cast<float>(frame.time - m_shorts.toggledAt) * kFeedbackFps
                         : 1e9f;
    if (!paused && at >= kPlayFade.back().frame) {
        return;
    }
    // Paused, the pause glyph pops, then the play button rests in its place.
    const bool showPause = paused && at < kPauseHold;
    float glyphScale = 1;
    float circleScale = 1;
    if (showPause || !paused) {
        frame.animating = true;
        glyphScale = keyed(kFeedbackGlyphScale, at);
        circleScale = keyed(kFeedbackCircleScale, at);
    }
    const float alpha = paused ? 1 : keyed(kPlayFade, at);
    ImDrawList* list = c.list();
    list->AddCircleFilled(center, c.dp(kFeedbackCircle) * 0.5f * circleScale,
                          motion::withAlpha(kFeedbackFill, alpha));
    const ImU32 glyph = motion::withAlpha(kOverlayText, alpha);
    if (showPause) {
        const float half = c.dp(kPauseBarsApart) * 0.5f * glyphScale;
        const ImVec2 bar{c.dp(kPauseBar.x) * 0.5f * glyphScale,
                         c.dp(kPauseBar.y) * 0.5f * glyphScale};
        for (const float x : {center.x - half, center.x + half}) {
            list->AddRectFilled({x - bar.x, center.y - bar.y}, {x + bar.x, center.y + bar.y}, glyph,
                                bar.x * 0.5f);
        }
        return;
    }
    // The triangle's centroid sits on the circle's centre, as in the official glyph.
    const ImVec2 size{c.dp(kPlayGlyph.x) * glyphScale, c.dp(kPlayGlyph.y) * glyphScale};
    const float left = center.x - size.x / 3;
    list->AddTriangleFilled({left, center.y - size.y * 0.5f}, {left, center.y + size.y * 0.5f},
                            {left + size.x, center.y}, glyph);
}

void App::drawShort(Frame& frame, const innertube::Item& item, bool current, ImVec2 min,
                    ImVec2 max) {
    const Canvas& c = frame.canvas;
    const Canvas medium = c.with(Weight::Medium);
    const float gutter = c.dp(theme::kGutter);
    const float width = max.x - min.x;
    innertube::Watch* info =
        current && m_watch.info && m_watch.info->videoId == item.id ? &*m_watch.info : nullptr;

    // Poster until the first frame is decoded, then the video itself.
    drawThumbnail(frame, posterUrl(item), min, max, 0, kPosterWidth);
    const auto state = m_player.state();
    if (current) {
        const bool started = state == media::PlayerState::Playing ||
                             state == media::PlayerState::Paused ||
                             state == media::PlayerState::Ended;
        if (started) {
            c.rect(min, max, IM_COL32_BLACK);
            // Vertical videos fill the page, cropping the sides; others are letterboxed.
            const float fill = m_videoSurface->fillZoom(min, max);
            m_videoSurface->draw(c.list(), min, max, fill <= kShortFillLimit ? fill : 1);
        }
        holdForSpeed(frame, min, max, min.y + m_insets.top + c.dp(theme::kGutter));
        if (state == media::PlayerState::Ended) {
            m_player.seek(0); // Shorts loop.
            m_player.setPaused(false);
        }
        if ((state == media::PlayerState::Loading || m_player.buffering()) && !m_shorts.scrub) {
            c.spinner({(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f}, theme::kSpinnerSize * 1.4f,
                      theme::g_textPrimary, frame.time);
            frame.animating = true;
        }
        // While playing, each new video frame wakes the loop and moves the bar; redrawing at the
        // display's rate on top would double the work.
    }
    if (current && m_shorts.scrub) {
        // While seeking only the video and the bar show, the bar red with a handle.
        const float barTop = max.y - c.dp(kProgressHeight);
        const float handle = min.x + width * *m_shorts.scrub;
        c.rect({min.x, barTop}, max, theme::g_track);
        c.rect({min.x, barTop}, {handle, max.y}, theme::kAccent);
        c.list()->AddCircleFilled({handle, barTop + c.dp(kProgressHeight) * 0.5f},
                                  c.dp(theme::kScrubHandle), theme::kAccent);
        return;
    }
    const float solidTop = max.y - c.dp(kScrimBottomSolid);
    c.list()->AddRectFilledMultiColor({min.x, solidTop - c.dp(kScrimBottomFade)}, {max.x, solidTop},
                                      kTransparent, kTransparent, kReelScrim, kReelScrim);
    c.rect({min.x, solidTop}, max, kReelScrim);
    c.list()->AddRectFilledMultiColor(min, {max.x, min.y + c.dp(kScrimTopFade)}, kReelScrim,
                                      kReelScrim, kTransparent, kTransparent);

    // Right-hand actions, stacked up from the title's baseline in the official order: like,
    // dislike, comments, share, then save where the official app has Remix (which records a
    // short). Those needing the video's details show (inert) before they arrive so nothing
    // jumps in later.
    const float actionX = max.x - c.dp(kActionAxis);
    const float captionHeight = c.lineHeight(theme::kCaptionSize);
    float slotBottom = max.y - gutter * 1.5f;
    const float shadow = c.dp(kShadowOffset);
    const Rating rating = info ? info->rating : Rating::None;
    // `like` draws the like button's own (animated) icon instead of `icon`.
    const auto action = [&](Icon icon, std::string_view label, bool needsInfo = true,
                            bool like = false) {
        const bool enabled = current && (info != nullptr || !needsInfo);
        const float iconY = slotBottom - captionHeight * 0.5f - c.dp(kActionLabel);
        const ImVec2 hitMin{actionX - c.dp(kActionWidth) * 0.5f,
                            iconY - c.dp(kActionIcon) * 0.5f - c.dp(theme::kGutter) * 0.5f};
        const ImVec2 hitMax{actionX + c.dp(kActionWidth) * 0.5f, slotBottom};
        slotBottom -= c.dp(kActionSpacing);
        const bool pressed = enabled && frame.touch.pressed(hitMin, hitMax);
        const ImVec2 center{actionX, iconY};
        if (pressed) {
            c.list()->AddCircleFilled(center, c.dp(kActionWidth) * 0.5f, kActionPressed);
            frame.animating = true;
        }
        const float scale = pressed ? kActionPressScale : 1;
        if (like) {
            drawLikeIcon(frame, rating == Rating::Like, center, scale);
        } else {
            c.icon(icon, {center.x, center.y + shadow}, kActionIcon * scale, kShadow);
            c.icon(icon, center, kActionIcon * scale, kOverlayText);
        }
        const float labelWidth = medium.measure(theme::kCaptionSize, label).x;
        // Centred under the icon, but kept on screen when longer than the column ("Dislike").
        const ImVec2 labelAt{std::min(center.x - labelWidth * 0.5f, max.x - gutter - labelWidth),
                             center.y + c.dp(kActionLabel) - captionHeight * 0.5f};
        medium.text({labelAt.x, labelAt.y + shadow}, theme::kCaptionSize, kShadow, label);
        medium.text(labelAt, theme::kCaptionSize, kOverlayText, label);
        return enabled && frame.touch.tapped(hitMin, hitMax);
    };
    if (action(Icon::ReelSave, tr(Text::Save), false)) {
        later([this, id = item.id] { openSave(id); });
    }
    if (action(Icon::ReelShare, tr(Text::Share), false)) {
        shareVideo(item, true);
    }
    if (action(Icon::ReelComment, info ? std::string_view(info->commentCount) : "")) {
        if (!info->commentsToken.empty()) {
            later([this] { openSheet(Sheet::Comments); });
        }
    }
    if (action(rating == Rating::Dislike ? Icon::ReelDislikeFilled : Icon::ReelDislike,
               tr(Text::Dislike))) {
        rateVideo(toggled(info->rating, Rating::Dislike));
    }
    if (action(Icon::ReelLike, info ? std::string_view(info->likes) : "", true, true)) {
        rateVideo(toggled(info->rating, Rating::Like));
    }

    // Channel and title, bottom left.
    // The title sits on the bottom margin whatever its length, as in the official app: a single
    // line stays close to the progress bar. Tapping it opens the description.
    const float textX = min.x + c.dp(kTextStart);
    const float textRight = max.x - c.dp(kTextEnd);
    const float textWidth = textRight - textX;
    const std::string& title = info ? info->title : item.title;
    float bottom = max.y - gutter * 1.5f;
    const float titleHeight =
        c.lineHeight(kTitleSize) *
        static_cast<float>(c.lineCount(textWidth, kTitleSize, title, kShortTitleLines));
    const ImVec2 titleMin{textX, bottom - titleHeight};
    c.paragraph({titleMin.x, titleMin.y + shadow}, textWidth, kTitleSize, kShadow, title,
                kShortTitleLines);
    c.paragraph(titleMin, textWidth, kTitleSize, kOverlayText, title, kShortTitleLines);
    if (info && current && frame.touch.tapped(titleMin, {textRight, bottom})) {
        later([this] { openSheet(Sheet::Description); });
    }
    bottom -= titleHeight + gutter * 0.75f;
    const float avatar = c.dp(kChannelAvatar);
    const float centerY = bottom - avatar * 0.5f;
    const float nameX = textX + avatar + c.dp(kBylineGap);
    const float nameY = centerY - c.lineHeight(theme::kBodySize) * 0.5f;
    const auto name = [&](std::string_view text, float maxWidth) {
        medium.line({nameX, nameY + shadow}, maxWidth, theme::kBodySize, kShadow, text);
        medium.line({nameX, nameY}, maxWidth, theme::kBodySize, kOverlayText, text);
    };
    if (info) {
        auto& channel = info->channel;
        drawAvatar(frame, channel.avatar, channel.name, {textX + avatar * 0.5f, centerY},
                   kChannelAvatar);
        const float nameWidth = medium.measure(theme::kBodySize, channel.name).x;
        const float maxName = std::max(0.0f, width * 0.45f);
        name(channel.name, maxName);
        if (current &&
            frame.touch.tapped({min.x, centerY - avatar * 0.5f},
                               {nameX + std::min(nameWidth, maxName), centerY + avatar * 0.5f})) {
            later([this, id = channel.id, name = channel.name] { openChannel(id, name); });
        }
        const std::string_view label =
            channel.subscribed ? tr(Text::Subscribed) : tr(Text::Subscribe);
        const float buttonX = nameX + std::min(nameWidth, maxName) + c.dp(kBylineGap);
        const float buttonWidth =
            c.with(theme::kLabelWeight).measure(theme::kBodySize, label).x + gutter * 2;
        const float half = c.dp(theme::kButtonHeight) * 0.45f;
        if (current) {
            drawSubscribeButton(frame, channel, {buttonX, centerY - half},
                                {buttonX + buttonWidth, centerY + half});
        }
    } else {
        drawAvatar(frame, itemAvatar(frame, item), item.channel, {textX + avatar * 0.5f, centerY},
                   kChannelAvatar);
        name(item.channel, width * 0.5f);
    }

    // Playback progress along the bottom edge, and tap anywhere else to pause.
    if (current) {
        const float duration = static_cast<float>(m_player.durationUs());
        if (duration > 0) {
            const float fraction =
                std::clamp(static_cast<float>(m_player.positionUs()) / duration, 0.0f, 1.0f);
            c.rect({min.x, max.y - c.dp(kProgressHeight)}, max, theme::g_track);
            c.rect({min.x, max.y - c.dp(kProgressHeight)}, {min.x + width * fraction, max.y},
                   theme::g_textPrimary);
        }
        drawPlayFeedback(frame, {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f},
                         state == media::PlayerState::Paused);
        // A second tap soon after likes the short, as in the official app, so a tap pauses only
        // once no second one can follow.
        auto& shorts = m_shorts;
        if (frame.touch.tapped(min, max)) {
            if (shorts.tapAt >= 0 && frame.time - shorts.tapAt < theme::kDoubleTap) {
                shorts.tapAt = -1;
                if (info && m_session.signedIn()) {
                    shorts.burstAt = frame.time;
                    shorts.burstCentre = frame.touch.position();
                    m_platform.haptic(Haptic::ContextClick);
                }
                if (info && info->rating != Rating::Like) {
                    rateVideo(Rating::Like); // Asks to sign in when signed out.
                }
            } else {
                shorts.tapAt = frame.time;
            }
        }
        if (shorts.tapAt >= 0 && frame.time - shorts.tapAt >= theme::kDoubleTap) {
            shorts.tapAt = -1;
            shorts.toggledAt = frame.time;
            m_player.setPaused(state == media::PlayerState::Playing);
        } else if (shorts.tapAt >= 0) {
            frame.animating = true;
        }
        drawLikeBurst(frame);
    }
}

} // namespace sloptube::app
