#pragma once

#include "core/innertube/Api.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace sloptube::app {

struct Upload {
    std::string videoId;
    std::string title;
    std::string channel;
};

// Videos of the signed-in account's subscriptions feed not seen by an earlier check, newest
// first and at most a few; the first check only records what is there. Runs in the background
// job (UploadsJob), without the app's other state, so it opens its own session from
// `dataDirectory`. Blocks on the network.
[[nodiscard]] std::vector<Upload> newUploads(const std::filesystem::path& dataDirectory,
                                             innertube::Locale locale);

} // namespace sloptube::app
