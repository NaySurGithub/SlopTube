#pragma once

#include "app/media/HttpSource.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace sloptube::http {
class Client;
}

namespace sloptube::app::media {

// One segment of a live stream: a complete fragmented MP4 file (ftyp, moov, moof, mdat).
struct Segment {
    std::int64_t sequence = 0;
    std::string data;
};

// The segments of a live stream ("&sq=<n>" URLs), fetched in order by a background thread a few
// segments ahead of the one being demuxed.
class LiveSource : public Source {
public:
    // Starts at segment `first`, or a few segments behind the live edge when it is negative.
    LiveSource(const http::Client& http, std::string url, std::int64_t first);
    ~LiveSource() override;

    LiveSource(const LiveSource&) = delete;
    LiveSource& operator=(const LiveSource&) = delete;

    // The next segment once downloaded. With `wait`, blocks until it is, or until the stream
    // ends or the source is cancelled.
    [[nodiscard]] std::optional<Segment> take(bool wait);

    // No further segment will come: the broadcast is over, or the stream failed.
    [[nodiscard]] bool ended() const;

    void cancel() override;
    void setBriefReadAhead(bool brief) override;

private:
    void fetchLoop();
    // The live edge's sequence number, or -1.
    [[nodiscard]] std::int64_t headSequence() const;

    const http::Client& m_http;
    std::string m_url;
    std::int64_t m_next; // Sequence number of the next segment to fetch; -1 until known.

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::deque<Segment> m_ready;
    bool m_ended = false;
    bool m_brief = false;
    std::atomic<bool> m_cancelled = false;
    std::thread m_fetcher;
};

} // namespace sloptube::app::media
