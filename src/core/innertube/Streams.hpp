#pragma once

#include "core/innertube/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Choice of playable streams among the formats of a player response.
namespace sloptube::innertube {

// One entry of the quality menu.
struct Quality {
    std::string label; // "720p", "1080p60".
    int lines = 0;     // Short side of the frame, the number in the label.
    int fps = 0;
    std::size_t video = 0;    // Index into Player::formats; muxed with audio when progressive.
    std::int64_t bitrate = 0; // Video bits per second.
};

struct Streams {
    std::vector<Quality> qualities; // Best first; empty when nothing is playable.
    // Separate audio stream shared by every quality; absent for progressive (muxed) streams.
    std::optional<std::size_t> audio;
};

struct StreamLimits {
    int maxLines = 0; // Qualities above this are not offered; 0 means no limit.
};

// Prefers adaptive H.264 (then VP9) video with AAC (then Opus) audio, one stream per resolution
// and frame rate; falls back to progressive streams when no complete adaptive pair exists.
[[nodiscard]] Streams selectStreams(const Player& player, StreamLimits limits = {});

struct AutoHints {
    int maxLines = 0;              // Largest useful resolution, e.g. the screen's short side.
    std::int64_t bandwidthBps = 0; // Measured network throughput; 0 when unknown.
};

// Index of the quality "Auto" plays: the best one the screen and the network can use.
[[nodiscard]] std::size_t autoQuality(const Streams& streams, const Player& player,
                                      AutoHints hints);

} // namespace sloptube::innertube
