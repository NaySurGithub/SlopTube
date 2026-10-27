#pragma once

#include "core/innertube/Types.hpp"
#include "core/util/Result.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sloptube::http {
class Client;
}

// Subtitles: picking a track for the user's preference, and fetching and timing its cues.
namespace sloptube::innertube {

// Part of a cue revealed `offsetMs` after the cue starts (auto-generated tracks reveal words as
// they are spoken).
struct CaptionSegment {
    std::int64_t offsetMs = 0;
    std::string text;
};

struct Cue {
    std::int64_t startMs = 0;
    std::int64_t endMs = 0;
    std::int64_t window = 0; // Cues sharing a non-zero window replace each other on screen.
    std::vector<CaptionSegment> segments;
};

// What the user asked for, carried from one video to the next.
struct CaptionChoice {
    std::string language;    // Empty: subtitles off.
    std::string translateTo; // Machine translation target; empty for the track as written.

    bool operator==(const CaptionChoice&) const = default;
};

// A track of one video, and the language it is translated into, if any.
struct CaptionSelection {
    std::size_t track = 0;
    std::string translateTo;

    bool operator==(const CaptionSelection&) const = default;
};

// Whether two BCP 47 codes name the same language ("fr" matches "fr-CA").
[[nodiscard]] bool sameLanguage(std::string_view a, std::string_view b);

// The track matching `choice` best: written subtitles before auto-generated ones, and a written
// track in the translation target before a machine translation.
[[nodiscard]] std::optional<CaptionSelection> resolve(const Captions& captions,
                                                      const CaptionChoice& choice);

// Without an explicit choice, written subtitles in the phone's language are shown, else none.
[[nodiscard]] std::optional<CaptionSelection> defaultSelection(const Captions& captions,
                                                               std::string_view phoneLanguage);

// The choice that `selection` expresses, to apply to later videos.
[[nodiscard]] CaptionChoice choiceOf(const Captions& captions,
                                     const std::optional<CaptionSelection>& selection);

// Address of `track`'s cues as a json3 document, machine-translated into `translateTo` if set.
[[nodiscard]] std::string timedTextUrl(const CaptionTrack& track, std::string_view translateTo);

// Cues of a timedtext "json3" document, ordered by start time.
[[nodiscard]] Result<std::vector<Cue>> parseCues(std::string& document);

[[nodiscard]] Result<std::vector<Cue>>
fetchCues(const http::Client& http, const CaptionTrack& track, std::string_view translateTo);

// Text on screen at `timeMs`, lines separated by '\n'; empty between cues.
[[nodiscard]] std::string captionText(std::span<const Cue> cues, std::int64_t timeMs);

} // namespace sloptube::innertube
