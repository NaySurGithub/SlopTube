#pragma once

#include "core/util/Result.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace sloptube::http {
class Client;
}

// Updates of the app itself, from its GitHub releases (see docs/releasing.md): finding a newer
// one, downloading its APK and checking it against the published SHA-256. Installing goes
// through Android's PackageInstaller (SlopTubeActivity.installUpdate).
namespace sloptube::app {

struct Release {
    std::string version; // "0.2.0".
    std::string notes;   // The release notes, Markdown as written on GitHub.
    std::string apkUrl;
    std::string checksumUrl; // The ".sha256" file published next to the APK.
    std::int64_t size = 0;   // Of the APK, in bytes.
};

// Whether `candidate` ("1.2.3", with or without a leading "v") is newer than `current`.
[[nodiscard]] bool newerVersion(std::string_view candidate, std::string_view current);

// The latest release, if it is newer than this build. Blocks on the network.
[[nodiscard]] Result<std::optional<Release>> newerRelease(const http::Client& http);

// Downloads `release`'s APK to `file`, in pieces so that `received` can show progress, and keeps
// it only if its SHA-256 matches the published one. Blocks on the network.
[[nodiscard]] Result<void> downloadRelease(const http::Client& http, const Release& release,
                                           const std::filesystem::path& file,
                                           std::atomic<std::int64_t>& received);

} // namespace sloptube::app
