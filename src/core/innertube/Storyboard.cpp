#include "core/innertube/Storyboard.hpp"

#include <algorithm>

namespace sloptube::innertube {
namespace {

constexpr std::string_view kSheetPlaceholder = "$M";
// A live sheet is served this many segments after its last one, with a segment to spare.
constexpr std::int64_t kLiveSheetDelaySegments = 4;

// The smallest level at least `minWidth` pixels wide, else the largest.
const StoryboardLevel& pickLevel(const Storyboard& storyboard, int minWidth) {
    const auto found = std::ranges::find_if(
        storyboard.levels, [&](const StoryboardLevel& level) { return level.width >= minWidth; });
    return found == storyboard.levels.end() ? storyboard.levels.back() : *found;
}

// Frame `index` of `level`, counted across its sheets.
StoryboardFrame frameAt(const StoryboardLevel& level, std::int64_t index) {
    const std::int64_t perSheet = level.columns * level.rows;
    const std::int64_t sheet = index / perSheet;
    const auto cell = static_cast<int>(index % perSheet);
    StoryboardFrame frame{
        .url = level.url,
        .sheetWidth = level.width * level.columns,
        .x = cell % level.columns * level.width,
        .y = cell / level.columns * level.height,
        .width = level.width,
        .height = level.height,
    };
    if (const auto at = frame.url.find(kSheetPlaceholder); at != std::string::npos) {
        frame.url.replace(at, kSheetPlaceholder.size(), std::to_string(sheet));
    }
    return frame;
}

} // namespace

std::optional<StoryboardFrame> storyboardFrame(const Storyboard& storyboard, int minWidth,
                                               std::int64_t timeMs, std::int64_t durationMs) {
    if (storyboard.levels.empty() || storyboard.live) { // Live frames go by segment.
        return std::nullopt;
    }
    const StoryboardLevel& level = pickLevel(storyboard, minWidth);
    const int perSheet = level.columns * level.rows;
    if (level.count <= 0 || perSheet <= 0) {
        return std::nullopt;
    }
    std::int64_t index = 0;
    if (level.intervalMs > 0) {
        index = timeMs / level.intervalMs;
    } else if (durationMs > 0) {
        index = timeMs * level.count / durationMs;
    }
    return frameAt(level, std::clamp<std::int64_t>(index, 0, level.count - 1));
}

std::optional<StoryboardFrame> liveStoryboardFrame(const Storyboard& storyboard,
                                                   std::int64_t segment, std::int64_t edge) {
    if (!storyboard.live || storyboard.levels.empty() || segment < 0) {
        return std::nullopt;
    }
    const StoryboardLevel& level = storyboard.levels.front();
    const std::int64_t perSheet = level.columns * level.rows;
    if (perSheet <= 0) {
        return std::nullopt;
    }
    const std::int64_t sheetEnd = (segment / perSheet + 1) * perSheet - 1;
    if (sheetEnd > edge - kLiveSheetDelaySegments) {
        return std::nullopt;
    }
    return frameAt(level, segment);
}

std::int64_t storyboardInterval(const Storyboard& storyboard, int minWidth,
                                std::int64_t durationMs) {
    if (storyboard.levels.empty() || storyboard.live) {
        return 0;
    }
    const StoryboardLevel& level = pickLevel(storyboard, minWidth);
    if (level.intervalMs > 0) {
        return level.intervalMs;
    }
    return level.count > 0 ? durationMs / level.count : 0;
}

} // namespace sloptube::innertube
