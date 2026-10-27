#include "app/ui/Subtitles.hpp"

#include "app/media/Log.hpp"
#include "app/util/TaskQueue.hpp"
#include "core/innertube/Api.hpp"

#include <android/log.h>

#include <fstream>
#include <ranges>

namespace sloptube::app {
namespace {

using innertube::CaptionSelection;

constexpr float kTextSize = 15;           // sp, below the video in portrait.
constexpr float kFullscreenTextSize = 22; // sp
constexpr float kMaxWidth = 0.86f;        // Fraction of the player width a line may span.
constexpr float kPadding = 6;             // dp each side of a line.
constexpr ImU32 kTextColor = IM_COL32(0xff, 0xff, 0xff, 0xff);
constexpr ImU32 kBoxColor = IM_COL32(0x08, 0x08, 0x08, 0xbf);

} // namespace

Subtitles::Subtitles(const http::Client& http, innertube::Api& api, TaskQueue& tasks,
                     std::filesystem::path choiceFile, std::string phoneLanguage)
    : m_http(http)
    , m_api(api)
    , m_tasks(tasks)
    , m_choiceFile(std::move(choiceFile))
    , m_phoneLanguage(std::move(phoneLanguage)) {
    loadChoice();
}

void Subtitles::open(const std::string& videoId) {
    close();
    m_videoId = videoId;
    m_tasks.run([this, videoId] {
        auto captions = m_api.captions(videoId);
        m_tasks.post([this, videoId, captions = std::move(captions)]() mutable {
            if (m_videoId != videoId || !captions) {
                return; // No subtitles is the usual outcome of a failure; nothing to report.
            }
            m_captions = std::move(*captions);
            apply(m_choice ? innertube::resolve(m_captions, *m_choice)
                           : innertube::defaultSelection(m_captions, m_phoneLanguage));
        });
    });
}

void Subtitles::openLocal(const std::string& videoId, innertube::Captions captions) {
    close();
    m_videoId = videoId;
    m_captions = std::move(captions);
    apply(m_choice ? innertube::resolve(m_captions, *m_choice)
                   : innertube::defaultSelection(m_captions, m_phoneLanguage));
}

void Subtitles::close() {
    m_videoId.clear();
    m_captions = {};
    apply(std::nullopt);
}

void Subtitles::select(std::optional<CaptionSelection> selection) {
    m_choice = innertube::choiceOf(m_captions, selection);
    saveChoice();
    apply(std::move(selection), true);
}

std::optional<Text> Subtitles::takeError() {
    return std::exchange(m_error, std::nullopt);
}

void Subtitles::apply(std::optional<CaptionSelection> selection, bool picked) {
    const std::uint64_t request = ++m_request;
    m_selection = std::move(selection);
    m_cues.reset();
    if (!m_selection || m_selection->track >= m_captions.tracks.size()) {
        return;
    }
    m_tasks.run([this, request, picked, track = m_captions.tracks[m_selection->track],
                 translateTo = m_selection->translateTo] {
        auto cues = innertube::fetchCues(m_http, track, translateTo);
        m_tasks.post([this, request, picked, cues = std::move(cues)]() mutable {
            if (request != m_request) {
                return;
            }
            if (!cues) {
                __android_log_print(ANDROID_LOG_WARN, media::kLogTag, "subtitles: %s",
                                    cues.error().message.c_str());
                // Machine translation is rate limited without a proof-of-origin token; the
                // track as written is the closest substitute. The user's choice is kept.
                const bool translated = m_selection && !m_selection->translateTo.empty();
                if (picked) { // Selections made on the user's behalf fail silently.
                    m_error = translated ? Text::TranslationFailed : Text::SubtitlesFailed;
                }
                if (translated) {
                    apply(CaptionSelection{.track = m_selection->track}, false);
                }
                return;
            }
            __android_log_print(ANDROID_LOG_INFO, media::kLogTag, "subtitles: %zu cues",
                                cues->size());
            m_cues = std::make_shared<const std::vector<innertube::Cue>>(std::move(*cues));
        });
    });
}

void Subtitles::loadChoice() {
    std::ifstream file(m_choiceFile);
    innertube::CaptionChoice choice;
    if (std::getline(file, choice.language) && std::getline(file, choice.translateTo)) {
        m_choice = std::move(choice);
    }
}

void Subtitles::saveChoice() const {
    if (m_choice) {
        std::ofstream(m_choiceFile, std::ios::trunc) << m_choice->language << '\n'
                                                     << m_choice->translateTo << '\n';
    }
}

const std::vector<Subtitles::Line>& Subtitles::layout(const Canvas& canvas, const std::string& text,
                                                      float width, float size) {
    if (text == m_layoutText && width == m_layoutWidth && size == m_layoutSize) {
        return m_lines;
    }
    m_layoutText = text;
    m_layoutWidth = width;
    m_layoutSize = size;
    m_lines.clear();
    for (const auto paragraph : std::views::split(std::string_view(text), '\n')) {
        Line line;
        for (const auto word : std::views::split(std::string_view(paragraph), ' ')) {
            const std::string_view token(word);
            if (token.empty()) {
                continue;
            }
            std::string candidate =
                line.text.empty() ? std::string(token) : line.text + ' ' + std::string(token);
            const float candidateWidth = canvas.measure(size, candidate).x;
            if (candidateWidth > width && !line.text.empty()) {
                m_lines.push_back(std::move(line));
                line = {.text = std::string(token), .width = canvas.measure(size, token).x};
            } else {
                line = {.text = std::move(candidate), .width = candidateWidth};
            }
        }
        if (!line.text.empty()) {
            m_lines.push_back(std::move(line));
        }
    }
    return m_lines;
}

void Subtitles::draw(const Canvas& canvas, ImVec2 min, ImVec2 max, float bottom, bool large,
                     std::int64_t timeUs) {
    if (!m_cues) {
        return;
    }
    const std::string text = innertube::captionText(*m_cues, timeUs / 1000);
    if (text.empty()) {
        return;
    }
    const float size = large ? kFullscreenTextSize : kTextSize;
    const float padding = canvas.dp(kPadding);
    const auto& lines = layout(canvas, text, (max.x - min.x) * kMaxWidth - padding * 2, size);
    const float lineHeight = canvas.lineHeight(size);
    const float centre = (min.x + max.x) * 0.5f;
    float y = bottom - lineHeight * static_cast<float>(lines.size());
    for (const auto& line : lines) {
        const float half = line.width * 0.5f;
        canvas.rect({centre - half - padding, y}, {centre + half + padding, y + lineHeight},
                    kBoxColor);
        canvas.text({centre - half, y}, size, kTextColor, line.text);
        y += lineHeight;
    }
}

} // namespace sloptube::app
