// Settings: playback in the background and in picture-in-picture, autoplay, upload
// notifications and the search history.

#include "app/App.hpp"
#include "app/DataFiles.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"
#include "core/BuildInfo.hpp"

#include <filesystem>

namespace sloptube::app {

void App::openSettings() {
    push(Screen{.route = Route::Settings, .title = std::string(tr(Text::Settings))});
}

void App::applySettings() {
    m_settings.save(m_platform.dataDirectory() / kSettingsFile);
    // The job is (re)scheduled every launch: the system drops it when the app is updated.
    m_platform.setUploadNotifications(m_settings.uploadNotifications);
}

void App::drawSettings(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const float width = max.x - min.x;
    const float bar = c.dp(theme::kTopBarHeight);
    const float row = c.dp(kSettingRowHeight);
    const ImVec2 contentMin{min.x, min.y + bar};

    struct Toggle {
        Text label;
        Text detail;
        bool Settings::* value;
    };

    constexpr std::array<Toggle, 4> kToggles{{
        {Text::BackgroundPlay, Text::BackgroundPlayDetail, &Settings::backgroundPlay},
        {Text::PictureInPicture, Text::PictureInPictureDetail, &Settings::pictureInPicture},
        {Text::Autoplay, Text::AutoplayDetail, &Settings::autoplay},
        {Text::UploadNotifications, Text::UploadNotificationsDetail,
         &Settings::uploadNotifications},
    }};
    const float content = row * static_cast<float>(kToggles.size() + 1) + c.dp(theme::kGutter) * 3 +
                          c.lineHeight(theme::kCaptionSize);
    screen.feed->scroller.update(frame.touch, contentMin, max, content, frame.seconds, c.dp(1));
    frame.animating |= screen.feed->scroller.moving();

    c.list()->PushClipRect(contentMin, max, true);
    float y = contentMin.y - screen.feed->scroller.offset();
    for (const Toggle& toggle : kToggles) {
        bool& value = m_settings.*toggle.value;
        if (switchRow(frame, tr(toggle.label), tr(toggle.detail), {min.x, y}, width, value) &&
            !screen.feed->scroller.moving()) {
            value = !value;
            later([this] { applySettings(); });
        }
        y += row;
    }

    // The search history, kept on the phone only.
    const bool empty = m_searchHistory.empty();
    if (switchRow(frame, tr(Text::ClearSearchHistory),
                  tr(empty ? Text::SearchHistoryEmpty : Text::ClearSearchHistoryDetail), {min.x, y},
                  width, std::nullopt) &&
        !empty) {
        m_searchHistory.clear();
        std::error_code error;
        std::filesystem::remove(m_platform.dataDirectory() / kSearchHistoryFile, error);
        showToast(tr(Text::SearchHistoryCleared));
    }
    y += row + c.dp(theme::kGutter) * 2;

    const std::string version = std::string(build::kAppName) + ' ' + std::string(build::kVersion);
    c.line({min.x + c.dp(theme::kGutter), y}, width - c.dp(theme::kGutter) * 2, theme::kCaptionSize,
           theme::kTextSecondary, version, TextAlign::Center);
    c.list()->PopClipRect();

    drawTitleBar(frame, screen.title, {min.x, 0}, width);
}

} // namespace sloptube::app
