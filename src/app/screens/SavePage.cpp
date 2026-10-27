// "Share" and "Save": the system share sheet with the video's link, and the sheet listing the
// account's playlists with a check for those holding the video, plus "New playlist".

#include "app/App.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"

#include <format>

namespace sloptube::app {
namespace {

constexpr std::string_view kVideoLink = "https://youtu.be/";
constexpr std::string_view kShortLink = "https://youtube.com/shorts/";
constexpr float kSheetTop = 24;         // dp
constexpr float kOptionRow = 56;        // dp
constexpr float kMaxVisibleRows = 6.5f; // A half row hints that the list scrolls.
constexpr float kFormHeight = 0.6f;     // Of the screen: the field stays above the keyboard.
constexpr float kChipGap = 8;           // dp

constexpr std::array kPrivacyLabels{Text::PrivacyPrivate, Text::PrivacyUnlisted,
                                    Text::PrivacyPublic};
static_assert(kPrivacyLabels.size() == innertube::kPrivacyNames.size());

std::string_view privacyLabel(innertube::Privacy privacy) {
    return tr(kPrivacyLabels[std::to_underlying(privacy)]);
}

} // namespace

void App::shareVideo(const innertube::Item& item, bool isShort) {
    m_platform.share(item.title, std::format("{}{}", isShort ? kShortLink : kVideoLink, item.id));
}

void App::openSave(const std::string& videoId) {
    if (!m_session.signedIn()) {
        showToast(tr(Text::SignInToAct));
        return;
    }
    m_save = SaveState{.videoId = videoId, .loading = true};
    openSheet(Sheet::Save);
    m_tasks.run([this, videoId] {
        auto options = m_api.saveOptions(videoId);
        m_tasks.post([this, videoId, options = std::move(options)]() mutable {
            if (m_save.videoId != videoId) {
                return;
            }
            m_save.loading = false;
            m_save.failed = !options;
            if (options) {
                m_save.options = std::move(*options);
            }
        });
    });
}

void App::toggleSaved(std::size_t index) {
    auto& option = m_save.options[index];
    const bool saved = !option.contains;
    option.contains = saved;
    showToast(std::vformat(tr(saved ? Text::SavedTo : Text::RemovedFrom),
                           std::make_format_args(option.title)));
    m_tasks.run([this, playlist = option.id, video = m_save.videoId, saved] {
        const bool ok = m_api.setSaved(playlist, video, saved).has_value();
        m_tasks.post([this, playlist, video, saved, ok] {
            if (ok) {
                return;
            }
            showToast(tr(Text::ActionFailed));
            if (m_save.videoId != video) {
                return;
            }
            for (auto& option : m_save.options) {
                if (option.id == playlist) {
                    option.contains = !saved;
                }
            }
        });
    });
}

void App::createPlaylist() {
    std::string title(m_save.title.data());
    if (title.empty() || m_save.submitting) {
        return;
    }
    m_save.submitting = true;
    m_tasks.run([this, title, privacy = m_save.privacy, video = m_save.videoId] {
        auto id = m_api.createPlaylist(title, privacy, video);
        m_tasks.post([this, title, privacy, video, id = std::move(id)]() mutable {
            if (m_save.videoId == video) {
                m_save.submitting = false;
            }
            if (!id) {
                showToast(tr(Text::ActionFailed));
                return;
            }
            showToast(std::vformat(tr(Text::SavedTo), std::make_format_args(title)));
            if (m_save.videoId == video) {
                m_save.options.insert(
                    m_save.options.begin() + (m_save.options.empty() ? 0 : 1),
                    {.id = std::move(*id), .title = title, .privacy = privacy, .contains = true});
                m_save.creating = false;
                m_save.title = {};
            }
            closeSheet();
        });
    });
}

float App::saveSheetHeight(const Canvas& c, float available) const {
    if (m_save.creating) {
        return available * kFormHeight;
    }
    const float rows = m_save.loading || m_save.failed
                           ? 2.0f
                           : std::min(static_cast<float>(m_save.options.size()), kMaxVisibleRows);
    // Title, the rows, a divider and "New playlist".
    return c.dp(kSheetTop) + c.lineHeight(theme::kSheetTitleSize) + c.dp(theme::kGutter) +
           c.dp(kOptionRow) * (rows + 1) + c.dp(4) + c.dp(theme::kGutter);
}

void App::drawSaveSheet(Frame& frame, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const Canvas medium = c.with(Weight::Medium);
    const float gutter = c.dp(theme::kGutter);
    const float row = c.dp(kOptionRow);
    const float iconX = min.x + gutter * 1.5f + c.dp(theme::kIconSize) * 0.5f;
    const float textX = min.x + gutter * 3 + c.dp(theme::kIconSize);

    float y = min.y + c.dp(kSheetTop);
    c.with(theme::kHeadingWeight)
        .text({min.x + gutter * 1.5f, y}, theme::kSheetTitleSize, theme::kTextPrimary,
              tr(m_save.creating ? Text::NewPlaylist : Text::SaveVideoTo));
    y += c.lineHeight(theme::kSheetTitleSize) + gutter;

    if (m_save.creating) {
        // Title field with "Create" beside it, then the privacy chips.
        const float fieldHeight = c.dp(theme::kSearchFieldHeight);
        const float button = c.dp(80);
        ImGui::SetCursorScreenPos({min.x + gutter * 1.5f, y});
        ImGui::SetNextItemWidth(max.x - gutter * 2.5f - button - (min.x + gutter * 1.5f));
        ImGui::PushFont(nullptr, c.dp(theme::kBodySize));
        const std::string hint(tr(Text::PlaylistTitleHint));
        if (m_save.focusTitle) {
            ImGui::SetKeyboardFocusHere();
            m_save.focusTitle = false;
        }
        bool submit =
            ImGui::InputTextWithHint("##playlist", hint.c_str(), m_save.title.data(),
                                     m_save.title.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopFont();
        const ImVec2 buttonMin{max.x - gutter - button, y};
        const ImVec2 buttonMax{max.x - gutter, y + fieldHeight};
        if (m_save.submitting) {
            c.spinner({(buttonMin.x + buttonMax.x) * 0.5f, (buttonMin.y + buttonMax.y) * 0.5f},
                      theme::kSpinnerSize, theme::kTextPrimary, frame.time);
            frame.animating = true;
        } else if (pillButton(frame, tr(Text::CreatePlaylist), buttonMin, buttonMax,
                              m_save.title[0] != 0)) {
            submit = true;
        }
        if (submit) {
            later([this] { createPlaylist(); });
        }
        y += fieldHeight + gutter;

        float x = min.x + gutter * 1.5f;
        const float chip = c.dp(theme::kChipHeight);
        for (std::size_t i = 0; i < kPrivacyLabels.size(); ++i) {
            const auto privacy = static_cast<innertube::Privacy>(i);
            const std::string_view label = tr(kPrivacyLabels[i]);
            const float width = c.measure(theme::kCaptionSize, label).x + gutter * 1.5f;
            const bool selected = m_save.privacy == privacy;
            const ImVec2 chipMin{x, y};
            const ImVec2 chipMax{x + width, y + chip};
            c.rect(chipMin, chipMax, selected ? theme::kTextPrimary : theme::kSurface,
                   theme::kCornerRadius);
            drawPressed(frame, chipMin, chipMax, theme::kCornerRadius);
            medium.text({x + gutter * 0.75f, y + (chip - c.lineHeight(theme::kCaptionSize)) * 0.5f},
                        theme::kCaptionSize, selected ? theme::kTextOnLight : theme::kTextPrimary,
                        label);
            if (frame.touch.tapped(chipMin, chipMax)) {
                m_save.privacy = privacy;
            }
            x += width + c.dp(kChipGap);
        }
        return;
    }

    // Playlists, scrolling when there are many, then "New playlist" pinned under them.
    const ImVec2 listMin{min.x, y};
    const ImVec2 listMax{max.x, max.y - row - c.dp(4) - gutter};
    if (m_save.loading) {
        c.spinner({(min.x + max.x) * 0.5f, (listMin.y + listMax.y) * 0.5f}, theme::kSpinnerSize,
                  theme::kTextPrimary, frame.time);
        frame.animating = true;
    } else if (m_save.failed) {
        c.line({min.x + gutter, (listMin.y + listMax.y - c.lineHeight(theme::kBodySize)) * 0.5f},
               max.x - min.x - gutter * 2, theme::kBodySize, theme::kTextSecondary,
               tr(Text::ActionFailed), TextAlign::Center);
    } else {
        auto& scroller = m_save.scroller;
        scroller.update(frame.touch, listMin, listMax,
                        row * static_cast<float>(m_save.options.size()), frame.seconds, c.dp(1));
        frame.animating |= scroller.moving();
        c.list()->PushClipRect(listMin, listMax, true);
        for (std::size_t i = 0; i < m_save.options.size(); ++i) {
            const float top = listMin.y + row * static_cast<float>(i) - scroller.offset();
            if (top + row < listMin.y || top > listMax.y) {
                continue;
            }
            const auto& option = m_save.options[i];
            const ImVec2 rowMin{min.x, std::max(top, listMin.y)};
            const ImVec2 rowMax{max.x, std::min(top + row, listMax.y)};
            drawPressed(frame, rowMin, rowMax);
            const float centerY = top + row * 0.5f;
            c.icon(option.contains ? Icon::CheckboxOn : Icon::CheckboxOff, {iconX, centerY},
                   theme::kIconSize, theme::kTextPrimary);
            const std::string_view privacy = privacyLabel(option.privacy);
            const float privacyWidth = c.measure(theme::kCaptionSize, privacy).x;
            c.line({textX, centerY - c.lineHeight(theme::kBodySize) * 0.5f},
                   max.x - textX - privacyWidth - gutter * 3, theme::kBodySize, theme::kTextPrimary,
                   option.title);
            c.text({max.x - gutter * 1.5f - privacyWidth,
                    centerY - c.lineHeight(theme::kCaptionSize) * 0.5f},
                   theme::kCaptionSize, theme::kTextSecondary, privacy);
            if (!scroller.moving() && frame.touch.tapped(rowMin, rowMax)) {
                later([this, i] { toggleSaved(i); });
            }
        }
        c.list()->PopClipRect();
    }

    y = listMax.y;
    c.rect({min.x, y}, {max.x, y + c.dp(1)}, theme::kDivider);
    y += c.dp(4);
    const ImVec2 rowMin{min.x, y};
    const ImVec2 rowMax{max.x, y + row};
    drawPressed(frame, rowMin, rowMax);
    c.icon(Icon::Add, {iconX, y + row * 0.5f}, theme::kIconSize, theme::kTextPrimary);
    medium.text({textX, y + (row - c.lineHeight(theme::kBodySize)) * 0.5f}, theme::kBodySize,
                theme::kTextPrimary, tr(Text::NewPlaylist));
    if (frame.touch.tapped(rowMin, rowMax)) {
        m_save.creating = true;
        m_save.focusTitle = true;
    }
}

} // namespace sloptube::app
