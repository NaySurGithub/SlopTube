#pragma once

#include <media/NdkMediaDataSource.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace sloptube::http {
class Client;
}

namespace sloptube::app::media {

// Network observations shared by every source of a player and kept across videos.
class NetworkStats {
public:
    void addTransfer(std::int64_t bytes, std::chrono::steady_clock::duration elapsed);

    // Smoothed download throughput in bits per second; 0 until something was measured.
    [[nodiscard]] std::int64_t bandwidthBps() const { return m_bandwidthBps; }

    // Whether a demuxer is waiting for data that has not arrived yet.
    [[nodiscard]] bool starved() const { return m_waitingReads > 0; }

private:
    friend class HttpSource;

    std::atomic<std::int64_t> m_bandwidthBps = 0;
    std::atomic<int> m_waitingReads = 0;
};

// A download feeding a demuxer, cancelled when the session it belongs to stops.
class Source {
public:
    virtual ~Source() = default;

    // Unblocks pending reads; subsequent reads fail.
    virtual void cancel() = 0;

    // Reads ahead only the minimum while `brief`, as for a video that is not playing yet.
    virtual void setBriefReadAhead(bool brief) = 0;
};

// Random-access view of a remote file for AMediaExtractor. Data is fetched in fixed-size blocks
// through ranged requests, with a background thread reading ahead of the playback position.
class HttpSource : public Source {
public:
    // `size` and `bitrate` may be 0 when unknown; the bitrate sizes the read-ahead window.
    HttpSource(const http::Client& http, std::string url, std::int64_t size, std::int64_t bitrate,
               NetworkStats& stats);
    ~HttpSource() override;

    HttpSource(const HttpSource&) = delete;
    HttpSource& operator=(const HttpSource&) = delete;

    [[nodiscard]] AMediaDataSource* dataSource() const { return m_dataSource; }

    void cancel() override;
    void setBriefReadAhead(bool brief) override;

private:
    using Block = std::string; // Response bodies are moved in without copying.

    [[nodiscard]] ssize_t readAt(std::int64_t offset, void* buffer, std::size_t size);
    [[nodiscard]] const Block* waitForBlock(std::int64_t index, std::unique_lock<std::mutex>& lock);
    void prefetchLoop();
    [[nodiscard]] bool fetch(std::int64_t index, Block& out) const;
    void evict();
    [[nodiscard]] std::int64_t readAheadBlocks() const; // Under m_mutex.

    const http::Client& m_http;
    std::string m_url;
    std::int64_t m_size;
    std::int64_t m_readAheadBlocks;
    NetworkStats& m_stats;
    AMediaDataSource* m_dataSource = nullptr;

    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::map<std::int64_t, Block> m_blocks;
    std::int64_t m_readBlock = 0;          // Block most recently requested by the extractor.
    std::int64_t m_lastBlock = -1;         // Index of the final block once known, else -1.
    std::atomic<bool> m_cancelled = false; // Also stops the prefetcher.
    bool m_failed = false;
    bool m_brief = false;
    std::thread m_prefetcher;
};

} // namespace sloptube::app::media
