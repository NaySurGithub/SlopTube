#include "app/media/HttpSource.hpp"

#include "app/util/Guard.hpp"
#include "core/http/Client.hpp"

#include <algorithm>
#include <cstring>

namespace sloptube::app::media {
namespace {

// Large enough to amortise request latency (the CDN caps ranges at 10 MB), small enough to start
// playback quickly.
constexpr std::int64_t kBlockSize = 1024 * 1024;
// Media time buffered ahead of the read position, bounded in blocks to cap memory use.
constexpr std::int64_t kReadAheadSeconds = 30;
constexpr std::int64_t kMinReadAheadBlocks = 2;
constexpr std::int64_t kMaxReadAheadBlocks = 16;
constexpr std::int64_t kKeepBehindBlocks = 2;
constexpr int kFetchAttempts = 3;
// Weight of the newest sample in the smoothed bandwidth.
constexpr double kBandwidthSmoothing = 0.3;
constexpr std::int64_t kBitsPerByte = 8;

} // namespace

void NetworkStats::addTransfer(std::int64_t bytes, std::chrono::steady_clock::duration elapsed) {
    const double seconds = std::chrono::duration<double>(elapsed).count();
    if (seconds <= 0) {
        return;
    }
    const double sample = static_cast<double>(bytes * kBitsPerByte) / seconds;
    std::int64_t previous = m_bandwidthBps;
    std::int64_t next = 0;
    do {
        next = static_cast<std::int64_t>(previous == 0 ? sample
                                                       : static_cast<double>(previous) *
                                                                 (1 - kBandwidthSmoothing) +
                                                             sample * kBandwidthSmoothing);
    } while (!m_bandwidthBps.compare_exchange_weak(previous, next));
}

HttpSource::HttpSource(const http::Client& http, std::string url, std::int64_t size,
                       std::int64_t bitrate, NetworkStats& stats)
    : m_http(http)
    , m_url(std::move(url))
    , m_size(size)
    , m_readAheadBlocks(std::clamp(bitrate / kBitsPerByte * kReadAheadSeconds / kBlockSize,
                                   kMinReadAheadBlocks, kMaxReadAheadBlocks))
    , m_stats(stats) {
    if (size > 0) {
        m_lastBlock = (size - 1) / kBlockSize;
    }

    m_dataSource = AMediaDataSource_new();
    AMediaDataSource_setUserdata(m_dataSource, this);
    AMediaDataSource_setReadAt(
        m_dataSource, [](void* self, off64_t offset, void* buffer, size_t size) -> ssize_t {
            return static_cast<HttpSource*>(self)->readAt(offset, buffer, size);
        });
    AMediaDataSource_setGetSize(m_dataSource, [](void* self) -> ssize_t {
        const std::int64_t size = static_cast<HttpSource*>(self)->m_size;
        return size > 0 ? static_cast<ssize_t>(size) : -1;
    });
    AMediaDataSource_setClose(m_dataSource, [](void*) {});

    m_prefetcher = std::thread([this] {
        if (!guarded("http source", [this] { prefetchLoop(); })) {
            {
                const std::scoped_lock lock(m_mutex);
                m_failed = true; // Reads fail instead of waiting forever.
            }
            m_changed.notify_all();
        }
    });
}

HttpSource::~HttpSource() {
    cancel();
    m_prefetcher.join();
    AMediaDataSource_delete(m_dataSource);
}

void HttpSource::cancel() {
    {
        const std::scoped_lock lock(m_mutex); // Pairs with waiters so the wake-up is not lost.
        m_cancelled = true;
    }
    m_changed.notify_all();
}

ssize_t HttpSource::readAt(std::int64_t offset, void* buffer, std::size_t size) {
    std::unique_lock lock(m_mutex);
    auto* out = static_cast<std::uint8_t*>(buffer);
    std::size_t copied = 0;

    while (copied < size) {
        const std::int64_t position = offset + static_cast<std::int64_t>(copied);
        const std::int64_t index = position / kBlockSize;
        if (m_lastBlock >= 0 && index > m_lastBlock) {
            break; // End of file.
        }
        const Block* block = waitForBlock(index, lock);
        if (block == nullptr) {
            return copied > 0 ? static_cast<ssize_t>(copied) : -1;
        }
        const auto within = static_cast<std::size_t>(position - index * kBlockSize);
        if (within >= block->size()) {
            break; // Short final block.
        }
        const std::size_t count = std::min(size - copied, block->size() - within);
        std::memcpy(out + copied, block->data() + within, count);
        copied += count;
    }
    return static_cast<ssize_t>(copied);
}

const HttpSource::Block* HttpSource::waitForBlock(std::int64_t index,
                                                  std::unique_lock<std::mutex>& lock) {
    if (index != m_readBlock) {
        m_readBlock = index;
        m_changed.notify_all();
    }
    while (true) {
        if (m_cancelled || m_failed) {
            return nullptr;
        }
        if (const auto found = m_blocks.find(index); found != m_blocks.end()) {
            return &found->second;
        }
        ++m_stats.m_waitingReads;
        m_changed.wait(lock);
        --m_stats.m_waitingReads;
    }
}

void HttpSource::prefetchLoop() {
    while (!m_cancelled) {
        std::int64_t wanted = -1;
        {
            std::unique_lock lock(m_mutex);
            m_changed.wait(lock, [&] {
                if (m_cancelled) {
                    return true;
                }
                const std::int64_t last =
                    m_lastBlock >= 0 ? std::min(m_readBlock + readAheadBlocks(), m_lastBlock)
                                     : m_readBlock + readAheadBlocks();
                for (std::int64_t index = m_readBlock; index <= last; ++index) {
                    if (!m_blocks.contains(index)) {
                        wanted = index;
                        return true;
                    }
                }
                return false;
            });
        }
        if (wanted < 0) {
            continue;
        }

        Block block;
        const bool ok = fetch(wanted, block);
        {
            const std::scoped_lock lock(m_mutex);
            if (!ok) {
                m_failed = true;
            } else {
                if (static_cast<std::int64_t>(block.size()) < kBlockSize) {
                    m_lastBlock = wanted;
                }
                m_blocks.emplace(wanted, std::move(block));
                evict();
            }
        }
        m_changed.notify_all();
    }
}

bool HttpSource::fetch(std::int64_t index, Block& out) const {
    const std::int64_t first = index * kBlockSize;
    std::string url = m_url;
    url += http::kRangeParameter;
    url += std::to_string(first);
    url += '-';
    url += std::to_string(first + kBlockSize - 1);

    for (int attempt = 0; attempt < kFetchAttempts && !m_cancelled; ++attempt) {
        const auto start = std::chrono::steady_clock::now();
        auto response = m_http.send({.url = url, .cancelled = &m_cancelled});
        if (response && response->ok()) {
            out = std::move(response->body);
            // Short blocks measure latency, and local files are not the network.
            if (static_cast<std::int64_t>(out.size()) == kBlockSize &&
                !m_url.starts_with(http::kFileScheme)) {
                m_stats.addTransfer(static_cast<std::int64_t>(out.size()),
                                    std::chrono::steady_clock::now() - start);
            }
            return true;
        }
    }
    return false;
}

void HttpSource::setBriefReadAhead(bool brief) {
    {
        const std::scoped_lock lock(m_mutex);
        m_brief = brief;
    }
    m_changed.notify_all();
}

std::int64_t HttpSource::readAheadBlocks() const {
    return m_brief ? kMinReadAheadBlocks : m_readAheadBlocks;
}

void HttpSource::evict() {
    const std::int64_t keepFrom = m_readBlock - kKeepBehindBlocks;
    const std::int64_t keepTo = m_readBlock + readAheadBlocks();
    std::erase_if(m_blocks, [&](const auto& entry) {
        return entry.first < keepFrom || entry.first > keepTo;
    });
}

} // namespace sloptube::app::media
