#pragma once

#include "core/innertube/Types.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace sloptube::innertube {

// One preview frame: a cell of a storyboard sheet.
struct StoryboardFrame {
    std::string url;    // Of the sheet.
    int sheetWidth = 0; // Full width of a sheet, in pixels (decode at least this wide).
    int x = 0;          // Cell rectangle in the sheet, in pixels.
    int y = 0;
    int width = 0;
    int height = 0;
};

// The frame shown for `timeMs` from the smallest level at least `minWidth` pixels wide (else
// the largest); none when the video has no storyboard or a live one.
[[nodiscard]] std::optional<StoryboardFrame> storyboardFrame(const Storyboard& storyboard,
                                                             int minWidth, std::int64_t timeMs,
                                                             std::int64_t durationMs);

// A live storyboard's frame for segment `segment`, `edge` being the live edge's. A sheet is made
// once all its segments are out (measured: 3 segments after its last), so the newest segments
// have none yet; asking for it then would only cache a failure.
[[nodiscard]] std::optional<StoryboardFrame>
liveStoryboardFrame(const Storyboard& storyboard, std::int64_t segment, std::int64_t edge);

// Time between two frames of the level storyboardFrame picks for `minWidth`; 0 without one, and
// for a live storyboard (a frame per segment).
[[nodiscard]] std::int64_t storyboardInterval(const Storyboard& storyboard, int minWidth,
                                              std::int64_t durationMs);

} // namespace sloptube::innertube
