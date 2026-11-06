#pragma once

#include <array>
#include <filesystem>

namespace sloptube::app {

// How much bigger the whole interface is drawn for each Settings::interfaceSize.
inline constexpr std::array<float, 3> kInterfaceScales{1.0f, 1.15f, 1.3f};
inline constexpr int kInterfaceSizes = static_cast<int>(kInterfaceScales.size());
inline constexpr int kLanguageChoices = 4;

// Preferences of the Settings screen, kept in the data directory.
struct Settings {
    bool backgroundPlay = true;      // Playback goes on when the app leaves the screen.
    bool pictureInPicture = true;    // ...in a small window over other apps.
    bool autoplay = true;            // The next video plays when one ends.
    bool uploadNotifications = true; // New uploads of subscriptions are notified.
    bool checkUpdates = true;        // Release builds look for a newer release at launch.
    bool audioOnly = false;          // Videos play without their picture, to save data.
    // Text and buttons: 0 normal, 1 large, 2 extra large (kInterfaceScales).
    int interfaceSize = 0;
    // 0 follows the phone, 1 English, 2 French, 3 Spanish.
    int language = 0;

    // Defaults for anything missing or unreadable.
    [[nodiscard]] static Settings load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;
};

} // namespace sloptube::app
