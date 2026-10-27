#pragma once

#include "core/innertube/Types.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace sloptube::http {
class Client;
}

namespace sloptube::innertube {
class Api;
}

namespace sloptube::app {

class TaskQueue;

enum class DownloadState : std::uint8_t {
    Queued,
    Downloading,
    Done,
    Failed,
};

struct Download {
    innertube::Item item; // Thumbnail and avatar point at the local copies once downloaded.
    DownloadState state = DownloadState::Queued;
    std::string quality; // "720p".
    std::int64_t bytes = 0;
    std::int64_t total = 0;
};

// Videos saved for offline viewing, one directory per video under `root`:
//   manifest.json   the item, the chosen streams and caption tracks; "complete" once finished
//   video, audio    the streams (one file for progressive formats), ".part" while incomplete
//   thumb, avatar   images shown in lists while offline
//   captions/N      json3 cues of track N
// One video downloads at a time, on its own thread, in ranged chunks; an interrupted download
// resumes from its partial files at the next launch.
class Downloads {
public:
    Downloads(const http::Client& http, innertube::Api& api, TaskQueue& tasks,
              std::filesystem::path root);
    ~Downloads();

    Downloads(const Downloads&) = delete;
    Downloads& operator=(const Downloads&) = delete;

    // Queues `item` at `player`'s quality `quality` (an index into selectStreams()).
    void start(const innertube::Item& item, const innertube::Player& player, std::size_t quality);
    // Queues a failed download again, resuming from what it already has.
    void retry(const std::string& videoId);
    // Cancels and deletes the download.
    void remove(const std::string& videoId);

    // Newest first.
    [[nodiscard]] std::vector<Download> list() const;
    [[nodiscard]] std::optional<Download> find(const std::string& videoId) const;
    [[nodiscard]] bool busy() const;

    // Streams and tracks of a finished download, with file:// URLs.
    [[nodiscard]] std::optional<innertube::Player> localPlayer(const std::string& videoId) const;
    [[nodiscard]] std::optional<innertube::Captions>
    localCaptions(const std::string& videoId) const;

private:
    struct Job;

    void load();
    void workerLoop();
    void run(Job& job);
    [[nodiscard]] bool fetchFile(Job& job, const innertube::Format& format,
                                 const std::filesystem::path& path);
    void saveManifest(const Job& job) const;
    void changed() const; // Wakes the UI to redraw progress.
    [[nodiscard]] Job* findJob(const std::string& videoId);
    [[nodiscard]] const Job* findJob(const std::string& videoId) const;

    const http::Client& m_http;
    innertube::Api& m_api;
    TaskQueue& m_tasks;
    std::filesystem::path m_root;

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<std::unique_ptr<Job>> m_jobs; // Newest first.
    std::string m_cancelled;                 // Video whose transfer must stop.
    bool m_stopping = false;
    std::thread m_thread; // Last: started once everything above exists.
};

} // namespace sloptube::app
