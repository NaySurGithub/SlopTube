#pragma once

#include <filesystem>

namespace sloptube::app {

// Preferences of the Settings screen, kept in the data directory.
struct Settings {
    bool backgroundPlay = true;       // Playback goes on when the app leaves the screen.
    bool pictureInPicture = true;     // ...in a small window over other apps.
    bool autoplay = true;             // The next video plays when one ends.
    bool uploadNotifications = false; // New uploads of subscriptions are notified.

    // Defaults for anything missing or unreadable.
    [[nodiscard]] static Settings load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;
};

} // namespace sloptube::app
