#include "app/notifications/Uploads.hpp"

#include "app/DataFiles.hpp"
#include "app/platform/Platform.hpp"
#include "core/auth/Session.hpp"
#include "core/http/Client.hpp"

#include <algorithm>
#include <fstream>
#include <unordered_set>

namespace sloptube::app {
namespace {

// More at once is a flood, not news.
constexpr std::size_t kMaxNotified = 5;
// Ids remembered across checks: far more than one feed page holds.
constexpr std::size_t kRemembered = 500;

std::vector<std::string> readLines(const std::filesystem::path& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty()) {
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

} // namespace

std::vector<Upload> newUploads(const std::filesystem::path& dataDirectory,
                               innertube::Locale locale) {
    // A copy of the session: a token refreshed here is saved there, so the app's file is never
    // written by two processes, nor overwritten with what the app changed meanwhile.
    const std::filesystem::path sessionCopy = dataDirectory / kUploadsSessionFile;
    std::error_code error;
    std::filesystem::copy_file(dataDirectory / kSessionFile, sessionCopy,
                               std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        return {};
    }
    const http::Client http({.caPath = std::string(Platform::trustedCertificates())});
    auth::Session session(http, sessionCopy);
    if (!session.signedIn()) {
        return {};
    }
    innertube::Api api(http, session, std::move(locale));
    auto page = api.feed(innertube::Feed::Subscriptions);
    if (!page) {
        return {};
    }

    const std::filesystem::path seenFile = dataDirectory / kNotifiedFile;
    const bool firstCheck = !std::filesystem::exists(seenFile, error);
    std::vector<std::string> seen = readLines(seenFile);
    const std::unordered_set<std::string> known(seen.begin(), seen.end());

    std::vector<Upload> uploads;
    for (const innertube::Item& item : page->items) {
        if ((item.kind != innertube::ItemKind::Video && item.kind != innertube::ItemKind::Live) ||
            known.contains(item.id)) {
            continue;
        }
        seen.push_back(item.id);
        if (!firstCheck && uploads.size() < kMaxNotified) {
            uploads.push_back({.videoId = item.id, .title = item.title, .channel = item.channel});
        }
    }
    if (seen.size() > kRemembered) {
        seen.erase(seen.begin(), seen.end() - static_cast<std::ptrdiff_t>(kRemembered));
    }
    std::ofstream out(seenFile, std::ios::trunc);
    for (const std::string& id : seen) {
        out << id << '\n';
    }
    return uploads;
}

} // namespace sloptube::app
