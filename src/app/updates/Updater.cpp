#include "app/updates/Updater.hpp"

#include "app/util/Files.hpp"
#include "core/BuildInfo.hpp"
#include "core/http/Client.hpp"
#include "core/json/Json.hpp"

#include <mbedtls/sha256.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <span>
#include <vector>

namespace sloptube::app {
namespace {

constexpr std::string_view kApiUrl = "https://api.github.com/repos/";
constexpr std::string_view kLatestPath = "/releases/latest";
// Pieces of the download: small enough for a smooth progress bar, large enough to stay fast.
constexpr std::int64_t kPieceBytes = 512 * 1024;
constexpr int kPieceAttempts = 3;
constexpr std::size_t kSha256Bytes = 32;

std::array<int, 3> versionParts(std::string_view version) {
    if (version.starts_with('v')) {
        version.remove_prefix(1);
    }
    std::array<int, 3> parts{};
    for (int& part : parts) {
        const auto [end, error] =
            std::from_chars(version.data(), version.data() + version.size(), part);
        if (error != std::errc{}) {
            break;
        }
        version.remove_prefix(static_cast<std::size_t>(end - version.data()));
        if (!version.starts_with('.')) {
            break;
        }
        version.remove_prefix(1);
    }
    return parts;
}

std::string hex(std::span<const unsigned char> bytes) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string out;
    for (const unsigned char byte : bytes) {
        out += kDigits[byte >> 4];
        out += kDigits[byte & 0x0F];
    }
    return out;
}

const std::vector<std::string>& apiHeaders() {
    static const std::vector<std::string> headers{
        "Accept: application/vnd.github+json",
        // GitHub's API refuses requests without a User-Agent.
        "User-Agent: " + std::string(build::kAppName) + '/' + std::string(build::kVersion),
    };
    return headers;
}

} // namespace

bool newerVersion(std::string_view candidate, std::string_view current) {
    return versionParts(candidate) > versionParts(current);
}

Result<std::optional<Release>> newerRelease(const http::Client& http) {
    std::string url(kApiUrl);
    url += build::kRepository;
    url += kLatestPath;
    auto response = http.send({.url = url, .headers = apiHeaders(), .retryOnStall = true});
    if (!response) {
        return std::unexpected(response.error());
    }
    if (!response->ok()) {
        return fail("updates: HTTP " + std::to_string(response->status));
    }
    auto root = json::parse(response->body);
    if (!root) {
        return std::unexpected(root.error());
    }
    Release release{
        .version = std::string(json::string(*root, "/tag_name")),
        .notes = std::string(json::string(*root, "/body")),
    };
    if (release.version.starts_with('v')) {
        release.version.erase(0, 1);
    }
    if (!newerVersion(release.version, build::kVersion)) {
        return std::optional<Release>{};
    }
    simdjson::dom::array assets;
    if (!root->at_pointer("/assets").get_array().get(assets)) {
        for (const auto asset : assets) {
            const auto name = json::string(asset, "/name");
            const auto download = json::string(asset, "/browser_download_url");
            if (name.ends_with(".apk")) {
                release.apkUrl = download;
                release.size = json::integer(asset, "/size");
            } else if (name.ends_with(".apk.sha256")) {
                release.checksumUrl = download;
            }
        }
    }
    if (release.apkUrl.empty() || release.checksumUrl.empty() || release.size <= 0) {
        return fail("updates: the release has no APK and checksum");
    }
    return std::optional{std::move(release)};
}

Result<void> downloadRelease(const http::Client& http, const Release& release,
                             const std::filesystem::path& file,
                             std::atomic<std::int64_t>& received) {
    // The published checksum first: "<64 hex digits>  <file name>".
    auto checksum = http.send({.url = release.checksumUrl, .retryOnStall = true});
    if (!checksum || !checksum->ok() || checksum->body.size() < kSha256Bytes * 2) {
        return fail("updates: no checksum");
    }
    std::string expected = checksum->body.substr(0, kSha256Bytes * 2);
    std::ranges::transform(expected, expected.begin(),
                           [](char ch) { return ch >= 'A' && ch <= 'F' ? ch - 'A' + 'a' : ch; });

    std::string apk;
    apk.reserve(static_cast<std::size_t>(release.size));
    received = 0;
    while (static_cast<std::int64_t>(apk.size()) < release.size) {
        const auto first = static_cast<std::int64_t>(apk.size());
        const std::int64_t last = std::min(first + kPieceBytes, release.size) - 1;
        Result<http::Response> piece = fail("updates: download failed");
        for (int attempt = 0; attempt < kPieceAttempts; ++attempt) {
            piece = http.send({
                .url = release.apkUrl,
                .headers = {"Range: bytes=" + std::to_string(first) + '-' + std::to_string(last)},
            });
            if (piece && piece->ok()) {
                break;
            }
        }
        if (!piece) {
            return std::unexpected(piece.error());
        }
        if (!piece->ok() || piece->body.empty()) {
            return fail("updates: HTTP " + std::to_string(piece->status));
        }
        if (piece->status == 200) {
            apk = std::move(piece->body); // The server ignored the range: the whole file came.
        } else {
            apk += piece->body;
        }
        received = static_cast<std::int64_t>(apk.size());
    }

    std::array<unsigned char, kSha256Bytes> digest{};
    mbedtls_sha256(reinterpret_cast<const unsigned char*>(apk.data()), apk.size(), digest.data(),
                   0);
    if (hex(digest) != expected) {
        return fail("updates: the download does not match its checksum");
    }
    if (!writeText(file, apk)) {
        return fail("updates: cannot store the download");
    }
    return {};
}

} // namespace sloptube::app
