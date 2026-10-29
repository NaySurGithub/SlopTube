#pragma once

#include "app/ui/Canvas.hpp"
#include "app/ui/Scroller.hpp"
#include "core/innertube/Types.hpp"
#include "core/util/Result.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace sloptube::app {

class Avatars;
class Touch;
class Thumbnails;

// Everything a view needs to draw and react to input during one frame.
struct Frame {
    Canvas canvas;
    Touch& touch;
    Thumbnails& thumbnails;
    Avatars& avatars;
    std::chrono::steady_clock::time_point now;
    double time = 0;        // Seconds since start, for animations.
    float seconds = 0;      // Duration of the previous frame.
    bool animating = false; // Set by views that need another frame soon.

    [[nodiscard]] float dp(float value) const { return canvas.dp(value); }
};

// A list of items backed by paginated InnerTube results.
struct Feed {
    std::function<Result<innertube::Page>()> request; // Re-run by "retry".
    innertube::Page page;
    bool loading = false;
    bool loaded = false;
    std::string error;
    Scroller scroller;
};

enum class ItemStyle : std::uint8_t {
    Card, // Full-width thumbnail with text below, as on the home feed.
    Row,  // Thumbnail on the left with text beside it, as in history and playlists.
};

// Draws `feed` virtualised inside [min, max] with an optional scrolling header, and returns the
// tapped item, if any. `nearEnd` fires when more items should be fetched.
struct ListHeader {
    float height = 0;
    std::function<void(Frame&, ImVec2 origin, float width)> draw;
};

// Full-width block scrolled between items, such as the Shorts shelf of the home feed. Inserts
// are given in increasing `after` order and draw (and handle taps) themselves.
struct ListInsert {
    std::size_t after = 0; // Items above it.
    float height = 0;
    std::function<void(Frame&, ImVec2 origin, float width)> draw;
};

std::optional<std::size_t> drawList(Frame& frame, Feed& feed, ItemStyle style, ImVec2 min,
                                    ImVec2 max, const ListHeader& header,
                                    const std::function<void()>& nearEnd,
                                    std::span<const ListInsert> inserts = {});

// Horizontally scrolling strip of small tiles; returns the tapped item.
std::optional<std::size_t> drawShelf(Frame& frame, Feed& feed, ImVec2 origin, float width);
[[nodiscard]] float shelfHeight(const Frame& frame);

// Remote image centre-cropped into the box, fading in once loaded.
void drawThumbnail(Frame& frame, const std::string& url, ImVec2 min, ImVec2 max, float roundingDp,
                   int maxWidth = 0);

// Duration (or "LIVE") overlay in the bottom-right corner of a thumbnail ending at
// `thumbnailMax`; nothing for empty text.
void drawDurationBadge(Frame& frame, std::string_view text, ImVec2 thumbnailMax);

// Round channel avatar from `url`, or a coloured initial while it is unknown or loading.
void drawAvatar(Frame& frame, const std::string& url, std::string_view name, ImVec2 center,
                float sizeDp);

// Avatar of an item's uploader, resolving it lazily when the feed did not provide one.
[[nodiscard]] const std::string& itemAvatar(Frame& frame, const innertube::Item& item);

// Material press feedback: a ripple spreading from the finger, clipped to the rectangle.
void drawPressed(Frame& frame, ImVec2 min, ImVec2 max, float roundingDp = 0);

// Shimmering placeholders shown while the first page of a feed loads.
void drawSkeleton(Frame& frame, ItemStyle style, ImVec2 min, ImVec2 max);

// Tap targets.
bool iconButton(Frame& frame, Icon icon, ImVec2 center, ImU32 color);
bool pillButton(Frame& frame, std::string_view label, ImVec2 min, ImVec2 max, bool primary);
// A section header with a chevron, opening the whole section.
bool textRow(Frame& frame, std::string_view label, ImVec2 origin, float width);
// A rounded chip at `origin`, filled white when selected (sort orders, filters, tabs); returns
// whether it was tapped. Its width is added to `x` with the gap that follows it.
bool chipButton(Frame& frame, std::string_view label, ImVec2 origin, bool selected, float& x);
// A settings row: a label and its explanation, with a switch on the right unless `on` is
// absent (an action); returns whether it was tapped. kSettingRowHeight tall.
bool switchRow(Frame& frame, std::string_view label, std::string_view detail, ImVec2 origin,
               float width, std::optional<bool> on);
inline constexpr float kSettingRowHeight = 72; // dp

// Centered status for empty, loading and failed feeds; returns whether "retry" was tapped.
bool drawFeedStatus(Frame& frame, const Feed& feed, ImVec2 min, ImVec2 max,
                    ItemStyle style = ItemStyle::Card);

[[nodiscard]] std::string formatDuration(std::int64_t seconds);

// What to tell the user about `error`: the server's own explanation as it is (already in their
// language), but a plain sentence instead of the app's technical messages ("http: ...",
// "player: ...").
[[nodiscard]] std::string_view friendlyError(std::string_view error);
[[nodiscard]] std::string imageUrl(const innertube::Item& item);

} // namespace sloptube::app
