// Shorts tab: full-screen vertical pager that plays the short in view.

#include "app/App.hpp"
#include "app/ui/Motion.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"
#include "app/ui/Touch.hpp"

#include <algorithm>
#include <cmath>
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
// Right-hand action column, measured on the official app: 60 dp between buttons, a 28 dp icon
// with its label centred 22 dp below, the column's axis 32 dp from the right edge.
constexpr float kActionSpacing = 60; // dp
constexpr float kActionIcon = 28;    // dp
constexpr float kActionLabel = 22;   // dp from the icon's centre to the label's centre.
constexpr float kActionAxis = 32;    // dp from the right edge.
constexpr float kActionWidth = 64;   // dp, width of a button's touch target.
constexpr float kShadowOffset = 1;   // dp, drop shadow keeping white glyphs legible on video.
constexpr ImU32 kShadow = IM_COL32(0, 0, 0, 0x70);
constexpr float kChannelAvatar = 32; // dp
constexpr float kScrimHeight = 0.4f; // Fraction of the page covered by the bottom scrim.
constexpr float kSheetTop = 0.3f;    // Fraction of the page above the comments and description.
constexpr float kProgressHeight = 2; // dp
constexpr int kPosterWidth = 720;    // px
constexpr int kShortTitleLines = 2;
// Double-tap like: a white thumb pops up amid rays of the accent colour, then fades. SlopTube's
// own animation; only the idea of a liking burst is the official app's.
constexpr float kBurstDuration = 0.8f; // s
constexpr float kBurstFade = 0.25f;    // s at the end.
constexpr float kBurstThumb = 64;      // dp
constexpr float kBurstInner = 40;      // dp from the centre where rays start.
constexpr float kBurstOuter = 72;      // dp where they end.
constexpr float kBurstStroke = 3;      // dp
constexpr std::size_t kBurstRays = 8;
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
           theme::kTextPrimary);
    c.with(theme::kHeadingWeight)
        .text(
            {origin.x + gutter * 1.75f + icon, centerY - c.lineHeight(theme::kSectionSize) * 0.5f},
            theme::kSectionSize, theme::kTextPrimary, tr(Text::Shorts));

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
                       theme::kTextPrimary, item.title, 2);
        // Hit box clamped to the list so tiles scrolled under the bars leave them their taps.
        const ImVec2 clipMin = c.list()->GetClipRectMin();
        const ImVec2 clipMax = c.list()->GetClipRectMax();
        if (frame.touch.tapped({x, std::max(y, clipMin.y)}, {max.x, std::min(max.y, clipMax.y)})) {
            later([this, index = first + i] { openShort(index); });
        }
    }
}

void App::drawShorts(Frame& frame, ImVec2 min, ImVec2 max) {
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
    const float elapsed = static_cast<float>(frame.time - m_shorts.burstAt);
    if (m_shorts.burstAt < 0 || elapsed > kBurstDuration) {
        m_shorts.burstAt = -1;
        return;
    }
    frame.animating = true;
    const Canvas& c = frame.canvas;
    const ImVec2 centre = m_shorts.burstCentre;
    const float t = elapsed / kBurstDuration;
    const float alpha = std::clamp((kBurstDuration - elapsed) / kBurstFade, 0.0f, 1.0f);

    // Rays: the head runs out over the first half, the tail catches up over the first 60 %.
    const float head = motion::easeOutCubic(t * 2);
    const float tail = motion::easeOutCubic(t / 0.6f);
    if (head > tail) {
        const float inner = c.dp(kBurstInner);
        const float span = c.dp(kBurstOuter - kBurstInner);
        for (std::size_t ray = 0; ray < kBurstRays; ++ray) {
            const float angle = static_cast<float>(ray) * 2 * std::numbers::pi_v<float> /
                                static_cast<float>(kBurstRays);
            const ImVec2 direction{std::cos(angle), std::sin(angle)};
            const auto at = [&](float along) {
                const float radius = inner + span * along;
                return ImVec2{centre.x + direction.x * radius, centre.y + direction.y * radius};
            };
            c.list()->AddLine(at(tail), at(head),
                              ray % 2 == 0 ? theme::kAccent : theme::kTextPrimary,
                              c.dp(kBurstStroke));
        }
    }
    const float pop =
        motion::easeOutBack(motion::progress(frame.time, m_shorts.burstAt, motion::kPopDuration));
    c.icon(Icon::ThumbUpFilled, centre, kBurstThumb * pop,
           motion::withAlpha(theme::kTextPrimary, alpha));
}

void App::drawShort(Frame& frame, const innertube::Item& item, bool current, ImVec2 min,
                    ImVec2 max) {
    const Canvas& c = frame.canvas;
    const Canvas medium = c.with(Weight::Medium);
    const float gutter = c.dp(theme::kGutter);
    const float width = max.x - min.x;
    const float page = max.y - min.y;
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
                      theme::kTextPrimary, frame.time);
            frame.animating = true;
        }
        // While playing, each new video frame wakes the loop and moves the bar; redrawing at the
        // display's rate on top would double the work.
    }
    if (current && m_shorts.scrub) {
        // While seeking only the video and the bar show, the bar red with a handle.
        const float barTop = max.y - c.dp(kProgressHeight);
        const float handle = min.x + width * *m_shorts.scrub;
        c.rect({min.x, barTop}, max, theme::kTrack);
        c.rect({min.x, barTop}, {handle, max.y}, theme::kAccent);
        c.list()->AddCircleFilled({handle, barTop + c.dp(kProgressHeight) * 0.5f},
                                  c.dp(theme::kScrubHandle), theme::kAccent);
        return;
    }
    c.list()->AddRectFilledMultiColor({min.x, max.y - page * kScrimHeight}, max, kTransparent,
                                      kTransparent, kScrimBottom, kScrimBottom);

    // Right-hand actions, stacked up from the title's baseline: share, save, comments, dislike,
    // like. Those needing the video's details show (inert) before they arrive so nothing jumps
    // in later.
    const float actionX = max.x - c.dp(kActionAxis);
    const float captionHeight = c.lineHeight(theme::kCaptionSize);
    float slotBottom = max.y - gutter * 1.5f;
    const float shadow = c.dp(kShadowOffset);
    const auto action = [&](Icon icon, std::string_view label, bool needsInfo = true) {
        const bool enabled = current && (info != nullptr || !needsInfo);
        const float iconY = slotBottom - captionHeight * 0.5f - c.dp(kActionLabel);
        const ImVec2 hitMin{actionX - c.dp(kActionWidth) * 0.5f,
                            iconY - c.dp(kActionIcon) * 0.5f - c.dp(theme::kGutter) * 0.5f};
        const ImVec2 hitMax{actionX + c.dp(kActionWidth) * 0.5f, slotBottom};
        slotBottom -= c.dp(kActionSpacing);
        const bool pressed = enabled && frame.touch.pressed(hitMin, hitMax);
        const ImVec2 center{actionX, iconY};
        c.icon(icon, {center.x + shadow, center.y + shadow}, kActionIcon, kShadow);
        c.icon(icon, center, pressed ? kActionIcon * 0.9f : kActionIcon, theme::kTextPrimary);
        const float labelWidth = medium.measure(theme::kCaptionSize, label).x;
        // Centred under the icon, but kept on screen when longer than the column ("Dislike").
        const ImVec2 labelAt{std::min(center.x - labelWidth * 0.5f, max.x - gutter - labelWidth),
                             center.y + c.dp(kActionLabel) - captionHeight * 0.5f};
        medium.text({labelAt.x + shadow, labelAt.y + shadow}, theme::kCaptionSize, kShadow, label);
        medium.text(labelAt, theme::kCaptionSize, theme::kTextPrimary, label);
        if (pressed) {
            frame.animating = true;
        }
        return enabled && frame.touch.tapped(hitMin, hitMax);
    };
    const Rating rating = info ? info->rating : Rating::None;
    if (action(Icon::Share, tr(Text::Share), false)) {
        shareVideo(item, true);
    }
    if (action(Icon::Save, tr(Text::Save), false)) {
        later([this, id = item.id] { openSave(id); });
    }
    if (action(Icon::Comment, info ? std::string_view(info->commentCount) : "")) {
        if (!info->commentsToken.empty()) {
            later([this] { openSheet(Sheet::Comments); });
        }
    }
    if (action(rating == Rating::Dislike ? Icon::ThumbDownFilled : Icon::ThumbDown,
               tr(Text::Dislike))) {
        rateVideo(toggled(info->rating, Rating::Dislike));
    }
    if (action(rating == Rating::Like ? Icon::ThumbUpFilled : Icon::ThumbUp,
               info ? std::string_view(info->likes) : "")) {
        rateVideo(toggled(info->rating, Rating::Like));
    }

    // Channel and title, bottom left.
    // The title sits on the bottom margin whatever its length, as in the official app: a single
    // line stays close to the progress bar. Tapping it opens the description.
    const float textRight = actionX - c.dp(32);
    const float textWidth = textRight - min.x - gutter;
    const std::string& title = info ? info->title : item.title;
    float bottom = max.y - gutter * 1.5f;
    const float titleHeight =
        c.lineHeight(theme::kBodySize) *
        static_cast<float>(c.lineCount(textWidth, theme::kBodySize, title, kShortTitleLines));
    const ImVec2 titleMin{min.x + gutter, bottom - titleHeight};
    c.paragraph(titleMin, textWidth, theme::kBodySize, theme::kTextPrimary, title,
                kShortTitleLines);
    if (info && current && frame.touch.tapped(titleMin, {textRight, bottom})) {
        later([this] { openSheet(Sheet::Description); });
    }
    bottom -= titleHeight + gutter * 0.75f;
    const float avatar = c.dp(kChannelAvatar);
    const float centerY = bottom - avatar * 0.5f;
    if (info) {
        auto& channel = info->channel;
        drawAvatar(frame, channel.avatar, channel.name, {min.x + gutter + avatar * 0.5f, centerY},
                   kChannelAvatar);
        const float nameX = min.x + gutter * 2 + avatar;
        const float nameWidth = medium.measure(theme::kBodySize, channel.name).x;
        const float maxName = std::max(0.0f, width * 0.45f);
        medium.line({nameX, centerY - c.lineHeight(theme::kBodySize) * 0.5f}, maxName,
                    theme::kBodySize, theme::kTextPrimary, channel.name);
        if (current &&
            frame.touch.tapped({min.x, centerY - avatar * 0.5f},
                               {nameX + std::min(nameWidth, maxName), centerY + avatar * 0.5f})) {
            later([this, id = channel.id, name = channel.name] { openChannel(id, name); });
        }
        const std::string_view label =
            channel.subscribed ? tr(Text::Subscribed) : tr(Text::Subscribe);
        const float buttonX = nameX + std::min(nameWidth, maxName) + gutter;
        const float buttonWidth =
            c.with(theme::kLabelWeight).measure(theme::kBodySize, label).x + gutter * 2;
        const float half = c.dp(theme::kButtonHeight) * 0.45f;
        if (current) {
            drawSubscribeButton(frame, channel, {buttonX, centerY - half},
                                {buttonX + buttonWidth, centerY + half});
        }
    } else {
        drawAvatar(frame, itemAvatar(frame, item), item.channel,
                   {min.x + gutter + avatar * 0.5f, centerY}, kChannelAvatar);
        medium.line({min.x + gutter * 2 + avatar, centerY - c.lineHeight(theme::kBodySize) * 0.5f},
                    width * 0.5f, theme::kBodySize, theme::kTextPrimary, item.channel);
    }

    // Playback progress along the bottom edge, and tap anywhere else to pause.
    if (current) {
        const float duration = static_cast<float>(m_player.durationUs());
        if (duration > 0) {
            const float fraction =
                std::clamp(static_cast<float>(m_player.positionUs()) / duration, 0.0f, 1.0f);
            c.rect({min.x, max.y - c.dp(kProgressHeight)}, max, theme::kTrack);
            c.rect({min.x, max.y - c.dp(kProgressHeight)}, {min.x + width * fraction, max.y},
                   theme::kTextPrimary);
        }
        if (state == media::PlayerState::Paused) {
            c.icon(Icon::Play, {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f},
                   theme::kPlayButtonSize, motion::withAlpha(theme::kTextPrimary, 0.8f));
        }
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
            m_player.setPaused(state == media::PlayerState::Playing);
        } else if (shorts.tapAt >= 0) {
            frame.animating = true;
        }
        drawLikeBurst(frame);
    }
}

} // namespace sloptube::app
