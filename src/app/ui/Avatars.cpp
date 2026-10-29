#include "app/ui/Avatars.hpp"

#include "app/util/TaskQueue.hpp"
#include "core/innertube/Api.hpp"

#include <fstream>

namespace sloptube::app {
namespace {

using innertube::Item;
using innertube::ItemKind;

// A lookup is one or two small anonymous requests (~15 KB for the uploader, ~30 KB for the
// avatar); three keep a screenful resolving quickly without starving thumbnails of workers.
constexpr int kMaxInFlight = 3;
// Older requests are dropped: their rows have most likely scrolled away.
constexpr std::size_t kMaxQueued = 24;
// The cache file holds one "<key> <url>" line per lookup. Past this many lines it is rewritten
// from the map (keeping the newest URL of each key), or started over when the map itself is
// that large.
constexpr std::size_t kMaxCacheLines = 4000;

} // namespace

Avatars::Avatars(innertube::Api& api, TaskQueue& tasks, std::filesystem::path cacheFile)
    : m_api(api)
    , m_tasks(tasks)
    , m_cacheFile(std::move(cacheFile)) {
    load();
}

const std::string& Avatars::get(const Item& item) {
    static const std::string kNone;
    const bool byChannel = !item.channelId.empty();
    if (!byChannel && item.kind != ItemKind::Video) {
        return kNone;
    }
    const std::string& key = byChannel ? item.channelId : item.id;
    const auto [found, inserted] = m_urls.try_emplace(key);
    if (inserted) {
        m_queue.push_back({.key = key, .channelId = item.channelId});
        if (m_queue.size() > kMaxQueued) {
            m_urls.erase(m_queue.front().key); // Forget it so it can be requested again later.
            m_queue.pop_front();
        }
        pump();
    }
    return found->second;
}

void Avatars::remember(const std::string& key, std::string url) {
    if (key.empty() || url.empty()) {
        return;
    }
    auto& known = m_urls[key];
    if (known != url) {
        known = std::move(url);
        store(key, known);
    }
}

void Avatars::pump() {
    while (m_inFlight < kMaxInFlight && !m_queue.empty()) {
        Lookup lookup = std::move(m_queue.back());
        m_queue.pop_back();
        ++m_inFlight;
        m_tasks.run([this, lookup = std::move(lookup), alive = std::weak_ptr(m_alive)]() mutable {
            if (lookup.channelId.empty()) {
                lookup.channelId = m_api.videoChannel(lookup.key).value_or("");
            }
            auto url = lookup.channelId.empty()
                           ? std::string()
                           : m_api.channelAvatar(lookup.channelId).value_or("");
            m_tasks.post([this, alive, lookup = std::move(lookup), url = std::move(url)] {
                if (alive.expired()) {
                    return;
                }
                --m_inFlight;
                if (!url.empty()) {
                    remember(lookup.channelId, url);
                    if (lookup.key != lookup.channelId) {
                        remember(lookup.key, url);
                    }
                }
                pump();
            });
        });
    }
}

void Avatars::load() {
    std::ifstream file(m_cacheFile);
    std::string key;
    std::string url;
    std::size_t lines = 0;
    while (file >> key >> url) {
        m_urls[key] = std::move(url);
        ++lines;
    }
    if (lines <= kMaxCacheLines) {
        return;
    }
    if (m_urls.size() > kMaxCacheLines) {
        m_urls.clear();
    }
    std::ofstream compacted(m_cacheFile, std::ios::trunc);
    for (const auto& [known, avatar] : m_urls) {
        compacted << known << ' ' << avatar << '\n';
    }
}

void Avatars::store(const std::string& key, const std::string& url) {
    // Appended on a worker: lookups land in bursts while scrolling, and the UI thread must not
    // wait on storage. Appends of one short line do not interleave.
    m_tasks.run([file = m_cacheFile, line = key + ' ' + url + '\n'] {
        std::ofstream(file, std::ios::app) << line;
    });
}

} // namespace sloptube::app
