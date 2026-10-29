#include "app/media/LiveSource.hpp"

#include "app/media/Log.hpp"
#include "core/http/Client.hpp"

#include <android/log.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <string_view>

namespace sloptube::app::media {
namespace {

using namespace std::chrono_literals;

constexpr std::string_view kSequenceParameter = "&sq=";
// Every segment carries an emsg box whose text names its sequence number.
constexpr std::string_view kSequenceField = "Sequence-Number: ";
constexpr std::size_t kSequenceSearchBytes = 4096;
constexpr std::ptrdiff_t kReadAheadSegments = 3;
constexpr std::ptrdiff_t kBriefReadAheadSegments = 1;
// A segment past the edge is held by the server until it exists, then sent; one asked for too
// early is answered with no content. A stream that keeps failing has ended.
constexpr auto kRetryDelay = 2s;

std::int64_t sequenceOf(std::string_view segment) {
    const std::string_view head = segment.substr(0, kSequenceSearchBytes);
    const auto at = head.find(kSequenceField);
    if (at == std::string_view::npos) {
        return -1;
    }
    const std::string_view digits = head.substr(at + kSequenceField.size());
    std::int64_t value = -1;
    std::from_chars(digits.data(), digits.data() + digits.size(), value);
    return value;
}

} // namespace

LiveSource::LiveSource(const http::Client& http, std::string url, std::int64_t first, int maxMisses)
    : m_http(http)
    , m_url(std::move(url))
    , m_next(first)
    , m_maxMisses(maxMisses) {
    m_fetcher = std::thread([this] { fetchLoop(); });
}

LiveSource::~LiveSource() {
    cancel();
    m_fetcher.join();
}

void LiveSource::cancel() {
    {
        const std::scoped_lock lock(m_mutex); // Pairs with waiters so the wake-up is not lost.
        m_cancelled = true;
    }
    m_changed.notify_all();
}

void LiveSource::setBriefReadAhead(bool brief) {
    {
        const std::scoped_lock lock(m_mutex);
        m_brief = brief;
    }
    m_changed.notify_all();
}

std::optional<Segment> LiveSource::take(bool wait) {
    std::unique_lock lock(m_mutex);
    if (wait) {
        m_changed.wait(lock, [&] { return m_cancelled || m_ended || !m_ready.empty(); });
    }
    if (m_cancelled || m_ready.empty()) {
        return std::nullopt;
    }
    Segment segment = std::move(m_ready.front());
    m_ready.pop_front();
    lock.unlock();
    m_changed.notify_all(); // Room for the fetcher.
    return segment;
}

bool LiveSource::ended() const {
    const std::scoped_lock lock(m_mutex);
    return m_ended && m_ready.empty();
}

std::int64_t LiveSource::headSequence() const {
    // Without a sequence number the server sends the newest segment.
    auto response = m_http.send({.url = m_url, .cancelled = &m_cancelled});
    if (!response || !response->ok()) {
        return -1;
    }
    return sequenceOf(response->body);
}

void LiveSource::fetchLoop() {
    if (m_next < 0) {
        const std::int64_t head = headSequence();
        if (head >= 0) {
            m_edge = head;
            m_next = std::max<std::int64_t>(head - kLiveStartBehindSegments, 0);
        } else {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "live: no edge segment");
        }
    }
    int misses = 0;
    while (m_next >= 0 && misses < m_maxMisses) {
        {
            std::unique_lock lock(m_mutex);
            m_changed.wait(lock, [&] {
                return m_cancelled || std::ssize(m_ready) <
                                          (m_brief ? kBriefReadAheadSegments : kReadAheadSegments);
            });
            if (m_cancelled) {
                return;
            }
        }
        std::string url = m_url;
        url += kSequenceParameter;
        url += std::to_string(m_next);
        // At the edge the server holds this until the segment exists, up to a segment's length.
        auto response = m_http.send({.url = url, .cancelled = &m_cancelled});
        if (response && response->ok() && !response->body.empty()) {
            misses = 0;
            {
                const std::scoped_lock lock(m_mutex);
                m_ready.push_back({.sequence = m_next, .data = std::move(response->body)});
            }
            m_changed.notify_all();
            ++m_next;
            continue;
        }
        ++misses;
        std::unique_lock lock(m_mutex);
        if (m_changed.wait_for(lock, kRetryDelay, [&] { return m_cancelled.load(); })) {
            return;
        }
    }
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "live: ended at segment %lld",
                        static_cast<long long>(m_next));
    {
        const std::scoped_lock lock(m_mutex);
        m_ended = true;
    }
    m_changed.notify_all();
}

} // namespace sloptube::app::media
