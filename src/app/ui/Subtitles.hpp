#pragma once

#include "app/ui/Canvas.hpp"
#include "app/ui/Strings.hpp"
#include "core/innertube/Captions.hpp"

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sloptube::http {
class Client;
}

namespace sloptube::innertube {
class Api;
}

namespace sloptube::app {

class TaskQueue;

// Subtitles of the video being watched: lists its tracks, picks one following the user's last
// choice (remembered across videos and launches), fetches its cues and draws them.
class Subtitles {
public:
    Subtitles(const http::Client& http, innertube::Api& api, TaskQueue& tasks,
              std::filesystem::path choiceFile, std::string phoneLanguage);

    void open(const std::string& videoId);
    // Opens the tracks of a downloaded video, read from disk; nothing can be translated.
    void openLocal(const std::string& videoId, innertube::Captions captions);
    void close();

    [[nodiscard]] const innertube::Captions& captions() const { return m_captions; }

    [[nodiscard]] const std::optional<innertube::CaptionSelection>& selection() const {
        return m_selection;
    }

    // Shows `selection` (or nothing) and remembers the choice for later videos.
    void select(std::optional<innertube::CaptionSelection> selection);

    // Message for the last failed track fetch, reported once.
    [[nodiscard]] std::optional<Text> takeError();

    // Draws the cues shown at `timeUs`, centred in [min.x, max.x] and stacked up from `bottom`.
    void draw(const Canvas& canvas, ImVec2 min, ImVec2 max, float bottom, bool large,
              std::int64_t timeUs);

private:
    struct Line {
        std::string text;
        float width = 0;
    };

    // Shows `selection`; a failure to fetch it is reported only when the user `picked` it.
    void apply(std::optional<innertube::CaptionSelection> selection, bool picked = false);
    void loadChoice();
    void saveChoice() const;
    // Wraps `text` into lines no wider than `width` (cached between frames).
    const std::vector<Line>& layout(const Canvas& canvas, const std::string& text, float width,
                                    float size);

    const http::Client& m_http;
    innertube::Api& m_api;
    TaskQueue& m_tasks;
    std::filesystem::path m_choiceFile;
    std::string m_phoneLanguage;
    std::optional<innertube::CaptionChoice> m_choice; // Unset until the user picks once.

    std::string m_videoId;
    innertube::Captions m_captions;
    std::optional<innertube::CaptionSelection> m_selection;
    std::shared_ptr<const std::vector<innertube::Cue>> m_cues;
    std::uint64_t m_request = 0; // Identifies the latest cue fetch; older results are dropped.
    std::optional<Text> m_error;

    std::string m_layoutText;
    float m_layoutWidth = 0;
    float m_layoutSize = 0;
    std::vector<Line> m_lines;
};

} // namespace sloptube::app
