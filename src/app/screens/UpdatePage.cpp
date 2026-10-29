// Updates of the app itself: a newer release is offered in a sheet, downloaded with its progress,
// checked against its published SHA-256, then handed to Android's installer.

#include "app/App.hpp"
#include "app/DataFiles.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"
#include "core/BuildInfo.hpp"

#include <format>

namespace sloptube::app {
namespace {

constexpr float kSheetTop = 24;      // dp above the title.
constexpr int kNotesLines = 5;       // Of the release notes shown.
constexpr float kProgressHeight = 6; // dp
constexpr float kButtonGap = 12;     // dp between the two buttons.

// The release notes as plain lines: headings, links to the full changelog and Markdown marks
// mean nothing to most readers.
std::string plainNotes(std::string_view notes) {
    std::string out;
    while (!notes.empty()) {
        const auto end = notes.find('\n');
        std::string_view line = notes.substr(0, end);
        notes.remove_prefix(end == std::string_view::npos ? notes.size() : end + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.remove_suffix(1);
        }
        if (line.empty() || line.starts_with('#') || line.starts_with("**Full Changelog**")) {
            continue;
        }
        if (line.starts_with("* ") || line.starts_with("- ")) {
            line.remove_prefix(2);
            out += "• ";
        }
        for (const char ch : line) {
            if (ch != '*' && ch != '`') {
                out += ch;
            }
        }
        out += '\n';
    }
    return out;
}

} // namespace

void App::checkForUpdate(bool manual) {
    if (!build::kChecksForUpdates || m_update.checking || m_update.downloading) {
        return;
    }
    m_update.checking = true;
    m_tasks.run([this, manual] {
        auto release = newerRelease(m_http);
        m_tasks.post([this, manual, release = std::move(release)]() mutable {
            m_update.checking = false;
            if (!release) {
                if (manual) {
                    showToast(friendlyError(release.error().message));
                }
                return;
            }
            if (!*release) {
                if (manual) {
                    showToast(tr(Text::UpToDate));
                }
                return;
            }
            m_update.available = std::move(**release);
            m_update.error.clear();
            // Offered at once when asked for, else once the launch animation is over.
            m_update.offered = manual;
            if (manual) {
                openSheet(Sheet::Update);
            }
        });
    });
}

void App::downloadUpdate() {
    if (!m_update.available || m_update.downloading) {
        return;
    }
    m_update.downloading = true;
    m_update.error.clear();
    m_update.received->store(0);
    const std::filesystem::path file = m_platform.dataDirectory() / kUpdateFile;
    m_tasks.run([this, release = *m_update.available, file, received = m_update.received] {
        auto done = downloadRelease(m_http, release, file, *received);
        m_tasks.post([this, file, done = std::move(done)] {
            m_update.downloading = false;
            if (!done) {
                m_update.error = done.error().message;
                return;
            }
            m_update.installing = true;
            m_platform.installUpdate(file);
        });
    });
}

float App::updateSheetHeight(const Canvas& c) const {
    const float gutter = c.dp(theme::kGutter);
    return c.dp(kSheetTop) + c.lineHeight(theme::kSheetTitleSize) + gutter +
           c.lineHeight(theme::kBodySize) * 2 + gutter +
           c.lineHeight(theme::kCaptionSize) * kNotesLines + gutter * 2 + c.dp(kProgressHeight) +
           gutter + c.dp(theme::kButtonHeight) + gutter * 2;
}

void App::drawUpdateSheet(Frame& frame, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const float gutter = c.dp(theme::kGutter);
    const float left = min.x + gutter * 1.5f;
    const float width = max.x - min.x - gutter * 3;
    if (!m_update.available) {
        return;
    }
    const Release& release = *m_update.available;

    float y = min.y + c.dp(kSheetTop);
    c.with(theme::kHeadingWeight)
        .text({left, y}, theme::kSheetTitleSize, theme::kTextPrimary, tr(Text::UpdateAvailable));
    y += c.lineHeight(theme::kSheetTitleSize) + gutter;

    const std::string_view current = build::kVersion;
    const std::string intro =
        std::vformat(tr(Text::UpdateIntro), std::make_format_args(release.version, current));
    y += c.paragraph({left, y}, width, theme::kBodySize, theme::kTextPrimary, intro, 2) + gutter;
    c.paragraph({left, y}, width, theme::kCaptionSize, theme::kTextSecondary,
                plainNotes(release.notes), kNotesLines);
    y += c.lineHeight(theme::kCaptionSize) * kNotesLines + gutter * 2;

    // Where it stands: the download's progress, the installer's turn, or what went wrong.
    std::string status;
    if (m_update.downloading) {
        const std::int64_t percent =
            release.size > 0 ? m_update.received->load() * 100 / release.size : 0;
        status = std::vformat(tr(Text::UpdateDownloading), std::make_format_args(percent));
        c.rect({left, y}, {left + width, y + c.dp(kProgressHeight)}, theme::kTrack,
               kProgressHeight * 0.5f);
        c.rect({left, y},
               {left + width * static_cast<float>(percent) / 100.0f, y + c.dp(kProgressHeight)},
               theme::kAccent, kProgressHeight * 0.5f);
        frame.animating = true;
    } else if (!m_update.error.empty()) {
        status = friendlyError(m_update.error);
    } else if (m_update.installing) {
        status = tr(Text::UpdateFollowSteps);
    }
    if (!status.empty()) {
        c.line({left, y - c.lineHeight(theme::kCaptionSize) - c.dp(4)}, width, theme::kCaptionSize,
               m_update.error.empty() ? theme::kTextSecondary : theme::kAccent, status);
    }
    y += c.dp(kProgressHeight) + gutter;

    // "Later" and the main action, as large as the sheet allows.
    const float buttonWidth = (width - c.dp(kButtonGap)) * 0.5f;
    const float buttonHeight = c.dp(theme::kButtonHeight);
    if (pillButton(frame, tr(Text::Later), {left, y}, {left + buttonWidth, y + buttonHeight},
                   false)) {
        later([this] { closeSheet(); });
    }
    if (!m_update.downloading) {
        const Text action = m_update.installing && m_update.error.empty() ? Text::UpdateInstall
                            : m_update.error.empty()                      ? Text::UpdateNow
                                                                          : Text::Retry;
        const ImVec2 actionMin{left + buttonWidth + c.dp(kButtonGap), y};
        if (pillButton(frame, tr(action), actionMin, {actionMin.x + buttonWidth, y + buttonHeight},
                       true)) {
            later([this] {
                // Downloaded already: Android's dialog was dismissed; show it again.
                const auto file = m_platform.dataDirectory() / kUpdateFile;
                std::error_code error;
                if (m_update.installing && m_update.error.empty() &&
                    std::filesystem::exists(file, error)) {
                    m_platform.installUpdate(file);
                } else {
                    m_update.installing = false;
                    downloadUpdate();
                }
            });
        }
    }
}

} // namespace sloptube::app
