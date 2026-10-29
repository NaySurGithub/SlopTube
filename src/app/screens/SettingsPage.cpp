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
    const float gutter = c.dp(theme::kGutter);
    const float sizeBlock = row + c.dp(theme::kChipHeight) + gutter;
    const float updateRows = build::kChecksForUpdates ? 2 : 0;
    const float content = sizeBlock + row * (static_cast<float>(kToggles.size() + 1) + updateRows) +
                          gutter * 3 + c.lineHeight(theme::kCaptionSize);
    screen.feed->scroller.update(frame.touch, contentMin, max, content, frame.seconds, c.dp(1));
    frame.animating |= screen.feed->scroller.moving();

    c.list()->PushClipRect(contentMin, max, true);
    float y = contentMin.y - screen.feed->scroller.offset();

    // Text and button size first: the setting that decides whether the rest can be read.
    (void)switchRow(frame, tr(Text::InterfaceSize), tr(Text::InterfaceSizeDetail), {min.x, y},
                    width, std::nullopt);
    y += row;
    constexpr std::array<Text, kInterfaceSizes> kSizeLabels{Text::SizeNormal, Text::SizeLarge,
                                                            Text::SizeExtraLarge};
    float x = min.x + gutter;
    for (int size = 0; size < kInterfaceSizes; ++size) {
        const bool selected = m_settings.interfaceSize == size;
        if (chipButton(frame, tr(kSizeLabels[static_cast<std::size_t>(size)]), {x, y}, selected,
                       x) &&
            !selected && !screen.feed->scroller.moving()) {
            m_settings.interfaceSize = size;
            later([this] {
                applySettings();
                applyScale();
            });
        }
    }
    y += c.dp(theme::kChipHeight) + gutter;

    for (const Toggle& toggle : kToggles) {
        bool& value = m_settings.*toggle.value;
        if (switchRow(frame, tr(toggle.label), tr(toggle.detail), {min.x, y}, width, value) &&
            !screen.feed->scroller.moving()) {
            value = !value;
            later([this] { applySettings(); });
        }
        y += row;
    }

    // Updates of the app, for builds that can install them (release builds).
    if (build::kChecksForUpdates) {
        if (switchRow(frame, tr(Text::CheckUpdatesAutomatically), tr(Text::CheckUpdatesDetail),
                      {min.x, y}, width, m_settings.checkUpdates) &&
            !screen.feed->scroller.moving()) {
            m_settings.checkUpdates = !m_settings.checkUpdates;
            later([this] { applySettings(); });
        }
        y += row;
        const std::string current =
            std::vformat(tr(Text::CurrentVersion), std::make_format_args(build::kVersion));
        if (switchRow(frame,
                      tr(m_update.checking ? Text::CheckingForUpdates : Text::CheckForUpdates),
                      current, {min.x, y}, width, std::nullopt) &&
            !screen.feed->scroller.moving()) {
            later([this] { checkForUpdate(true); });
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
    y += row + gutter * 2;

    const std::string version = std::string(build::kAppName) + ' ' + std::string(build::kVersion);
    c.line({min.x + gutter, y}, width - gutter * 2, theme::kCaptionSize, theme::kTextSecondary,
           version, TextAlign::Center);
    c.list()->PopClipRect();

    drawTitleBar(frame, screen.title, {min.x, 0}, width);
}

} // namespace sloptube::app
