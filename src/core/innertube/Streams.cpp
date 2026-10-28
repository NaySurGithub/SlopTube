#include "core/innertube/Streams.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <string_view>
#include <tuple>
#include <utility>

namespace sloptube::innertube {
namespace {

// Codec prefixes with their preference (lower is better); anything else (AV1, AC-3...) is
// skipped. H.264 decodes in hardware everywhere, VP9 on most devices; HE-AAC is the low-bitrate
// fallback.
using CodecRanks = std::array<std::pair<std::string_view, int>, 3>;
constexpr CodecRanks kVideoCodecs{{{"avc1", 0}, {"vp9", 1}, {"vp09", 1}}};
constexpr CodecRanks kAudioCodecs{{{"mp4a.40.2", 0}, {"opus", 1}, {"mp4a.40.5", 2}}};

// Higher-bitrate audio is surround, wasted on a phone.
constexpr std::int64_t kMaxStereoAudioBitrate = 200'000;
// Frame rates above this are labelled, as in "1080p60".
constexpr int kStandardFps = 30;
// Without a throughput measurement, Auto starts at this resolution.
constexpr int kUnmeasuredLines = 720;
// Share of the measured throughput streams may use, leaving room for fluctuations.
constexpr double kBandwidthHeadroom = 0.6;

constexpr int kUnsupported = std::numeric_limits<int>::max();

int codecRank(const Format& format, const CodecRanks& codecs) {
    constexpr std::string_view kCodecsKey = "codecs=\"";
    const auto at = format.mimeType.find(kCodecsKey);
    if (at == std::string::npos) {
        return kUnsupported;
    }
    const std::string_view codec = std::string_view(format.mimeType).substr(at + kCodecsKey.size());
    for (const auto& [prefix, rank] : codecs) {
        if (codec.starts_with(prefix)) {
            return rank;
        }
    }
    return kUnsupported;
}

// Files must have a known size; live segments have none.
bool fetchable(const Format& format) {
    return !format.url.empty() && (format.contentLength > 0 || format.segmented());
}

// The server's label ("1080p60", or "480p" for a 1080x608 frame); synthesised when absent.
std::string label(const Format& format) {
    if (!format.qualityLabel.empty()) {
        return format.qualityLabel;
    }
    std::string text = std::to_string(std::min(format.width, format.height)) + 'p';
    if (format.fps > kStandardFps) {
        text += std::to_string(format.fps);
    }
    return text;
}

int leadingNumber(std::string_view text) {
    int value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

std::optional<std::size_t> bestAudio(const std::vector<Format>& formats) {
    std::optional<std::size_t> best;
    const auto key = [&](const Format& f) {
        // Smaller is better: original track, uncompressed dynamics, codec, then stereo bitrates
        // from high to low before surround ones.
        const bool surround = f.bitrate > kMaxStereoAudioBitrate;
        return std::tuple{!f.defaultAudio, f.drc, codecRank(f, kAudioCodecs), surround,
                          surround ? f.bitrate : -f.bitrate};
    };
    for (std::size_t i = 0; i < formats.size(); ++i) {
        const Format& format = formats[i];
        if (format.hasVideo() || !format.hasAudio() || !fetchable(format) ||
            codecRank(format, kAudioCodecs) == kUnsupported) {
            continue;
        }
        if (!best || key(format) < key(formats[*best])) {
            best = i;
        }
    }
    return best;
}

// One quality per label among formats accepted by `eligible`.
template <typename Eligible>
std::vector<Quality> qualities(const std::vector<Format>& formats, Eligible eligible) {
    std::vector<Quality> result;
    for (std::size_t i = 0; i < formats.size(); ++i) {
        const Format& format = formats[i];
        if (!format.hasVideo() || !fetchable(format) || !eligible(format)) {
            continue;
        }
        std::string name = label(format);
        const int lineCount = leadingNumber(name);
        const Quality candidate{
            .label = std::move(name),
            .lines = lineCount,
            .fps = format.fps,
            .video = i,
            .bitrate = format.bitrate,
        };
        // Within one label: the preferred codec, then the higher bitrate (YouTube also serves
        // "data saver" variants).
        const auto key = [&](const Quality& q) {
            return std::pair{codecRank(formats[q.video], kVideoCodecs), -q.bitrate};
        };
        const auto same = std::ranges::find(result, candidate.label, &Quality::label);
        if (same == result.end()) {
            result.push_back(candidate);
        } else if (key(candidate) < key(*same)) {
            *same = candidate;
        }
    }
    std::ranges::sort(result, [](const Quality& a, const Quality& b) {
        return a.lines != b.lines ? a.lines > b.lines : a.fps > b.fps;
    });
    return result;
}

} // namespace

Streams selectStreams(const Player& player, StreamLimits limits) {
    Streams streams;
    streams.audio = bestAudio(player.formats);
    if (streams.audio) {
        streams.qualities = qualities(player.formats, [](const Format& f) {
            return !f.hasAudio() && codecRank(f, kVideoCodecs) != kUnsupported;
        });
    }
    if (streams.qualities.empty()) {
        streams.audio.reset();
        streams.qualities = qualities(player.formats, [](const Format& f) { return f.hasAudio(); });
    }
    if (limits.maxLines > 0 && streams.qualities.size() > 1) {
        // Keep at least the lowest quality even when it exceeds the limit.
        const auto keep = std::ranges::find_if(
            streams.qualities, [&](const Quality& q) { return q.lines <= limits.maxLines; });
        streams.qualities.erase(streams.qualities.begin(),
                                keep == streams.qualities.end() ? keep - 1 : keep);
    }
    return streams;
}

std::size_t autoQuality(const Streams& streams, const Player& player, AutoHints hints) {
    if (streams.qualities.empty()) {
        return 0;
    }
    int maxLines = hints.maxLines > 0 ? hints.maxLines : std::numeric_limits<int>::max();
    std::int64_t budget = std::numeric_limits<std::int64_t>::max();
    if (hints.bandwidthBps > 0) {
        budget =
            static_cast<std::int64_t>(static_cast<double>(hints.bandwidthBps) * kBandwidthHeadroom);
        if (streams.audio) {
            budget -= player.formats[*streams.audio].bitrate;
        }
    } else {
        maxLines = std::min(maxLines, kUnmeasuredLines);
    }
    for (std::size_t i = 0; i < streams.qualities.size(); ++i) {
        const Quality& quality = streams.qualities[i];
        if (quality.lines <= maxLines && quality.bitrate <= budget) {
            return i;
        }
    }
    return streams.qualities.size() - 1;
}

} // namespace sloptube::innertube
