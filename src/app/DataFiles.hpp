#pragma once

#include <string_view>

// Files kept in the app's private data directory, shared by the app and its background job.
namespace sloptube::app {

inline constexpr std::string_view kSessionFile = "session.json";
inline constexpr std::string_view kAvatarCacheFile = "avatars.txt";
inline constexpr std::string_view kSubtitlesFile = "subtitles.txt";
inline constexpr std::string_view kDownloadsDirectory = "downloads";
inline constexpr std::string_view kSettingsFile = "settings.json";
inline constexpr std::string_view kSearchHistoryFile = "searches.txt";
inline constexpr std::string_view kNotifiedFile = "notified.txt"; // Uploads already seen.
inline constexpr std::string_view kUpdateFile = "update.apk";     // A downloaded update.
// The background job's copy of the session, so it never writes the app's.
inline constexpr std::string_view kUploadsSessionFile = "uploads-session.json";

} // namespace sloptube::app
