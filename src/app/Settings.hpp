#pragma once

#include <array>
#include <filesystem>

namespace sloptube::app {

// How much bigger the whole interface is drawn for each Settings::interfaceSize.
inline constexpr std::array<float, 3> kInterfaceScales{1.0f, 1.15f, 1.3f};
inline constexpr int kInterfaceSizes = static_cast<int>(kInterfaceScales.size());

// Preferences of the Settings screen, kept in the data directory.
struct Settings {
    bool backgroundPlay = true;       // Playback goes on when the app leaves the screen.
    bool pictureInPicture = true;     // ...in a small window over other apps.
    bool autoplay = true;             // The next video plays when one ends.
    bool uploadNotifications = false; // New uploads of subscriptions are notified.
    // Text and buttons: 0 normal, 1 large, 2 extra large (kInterfaceScales).
    int interfaceSize = 0;

    // Defaults for anything missing or unreadable.
    [[nodiscard]] static Settings load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;
};

} // namespace sloptube::app
