#pragma once

#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace sloptube::innertube {
class Api;
struct Item;
} // namespace sloptube::innertube

namespace sloptube::app {

class TaskQueue;

// Resolves uploader avatars for feeds whose renderers carry none. Items are keyed by their
// channel id, or by their video id when the feed does not link the channel (the TV home feed),
// in which case the uploader is looked up first. Lookups run in the background, most recently
// requested first, and every URL learnt is appended to `cacheFile` so that channels and videos
// seen in an earlier session show their avatar at once.
class Avatars {
public:
    Avatars(innertube::Api& api, TaskQueue& tasks, std::filesystem::path cacheFile);

    // Avatar URL of the item's uploader, or an empty string while unknown (which queues a
    // lookup).
    [[nodiscard]] const std::string& get(const innertube::Item& item);

    // Records an avatar learnt from another response; `key` is a channel or video id.
    void remember(const std::string& key, std::string url);

private:
    struct Lookup {
        std::string key;
        std::string channelId; // Empty: resolve it from `key`, a video id.
    };

    void pump();
    void load();
    void store(const std::string& key, const std::string& url);

    innertube::Api& m_api;
    TaskQueue& m_tasks;
    std::filesystem::path m_cacheFile;
    std::unordered_map<std::string, std::string> m_urls; // Empty value: lookup queued or failed.
    std::deque<Lookup> m_queue;
    int m_inFlight = 0;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace sloptube::app
