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

// Playback starts this many segments behind the live edge, so the next one is always out before
// it is needed (about 15 s with 5 s segments, as the official app).
inline constexpr std::int64_t kLiveStartBehindSegments = 3;

// Failed fetches (2 s apart) before a stream counts as ended: a broadcast that stopped, or, for a
// source opened by a seek, segments the server no longer keeps, which must not hold playback up.
inline constexpr int kLiveMaxMisses = 10;
inline constexpr int kLiveSeekMisses = 2;

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
    LiveSource(const http::Client& http, std::string url, std::int64_t first,
               int maxMisses = kLiveMaxMisses);
    ~LiveSource() override;

    LiveSource(const LiveSource&) = delete;
    LiveSource& operator=(const LiveSource&) = delete;

    // The next segment once downloaded. With `wait`, blocks until it is, or until the stream
    // ends or the source is cancelled.
    [[nodiscard]] std::optional<Segment> take(bool wait);

    // No further segment will come: the broadcast is over, or the stream failed.
    [[nodiscard]] bool ended() const;

    // The live edge's sequence number when this source started near it, else -1. Known once
    // take(true) has returned a segment.
    [[nodiscard]] std::int64_t edge() const { return m_edge; }

    void cancel() override;
    void setBriefReadAhead(bool brief) override;

private:
    void fetchLoop();
    // The live edge's sequence number, or -1.
    [[nodiscard]] std::int64_t headSequence() const;

    const http::Client& m_http;
    std::string m_url;
    std::int64_t m_next; // Sequence number of the next segment to fetch; -1 until known.
    int m_maxMisses;

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::deque<Segment> m_ready;
    bool m_ended = false;
    bool m_brief = false;
    std::atomic<bool> m_cancelled = false;
    std::atomic<std::int64_t> m_edge = -1;
    std::thread m_fetcher;
};

} // namespace sloptube::app::media
