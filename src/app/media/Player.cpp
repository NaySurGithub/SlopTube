#include "app/media/Player.hpp"

#include "app/media/LiveSource.hpp"
#include "app/media/Log.hpp"
#include "app/media/TimeStretch.hpp"
#include "app/util/Guard.hpp"

#include <aaudio/AAudio.h>
#include <android/hardware_buffer.h>
#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string_view>
#include <sys/resource.h>
#include <unistd.h>
#include <vector>

namespace sloptube::app::media {
namespace {

using namespace std::chrono_literals;

constexpr std::int64_t kMicrosPerSecond = 1'000'000;
constexpr std::int64_t kRenderAheadUs = 10'000; // Present frames this early to hit vsync.
constexpr std::int64_t kLateDropUs = 100'000;   // Frames later than this are skipped.
constexpr auto kIdleWait = 4ms;
constexpr auto kPausedWait = 15ms;
constexpr std::int32_t kReaderMaxImages = 4; // Codec working set plus one held by the UI.
constexpr double kAudioBufferSeconds = 0.5;
// The hardware decoders of the target phones top out at 1080p60.
constexpr int kMaxDecodableLines = 1080;
// A prepared quality switch further than this from the playback position seeks again.
constexpr std::int64_t kSwitchResyncUs = 2'000'000;

// Lock-free single-producer/single-consumer queue of interleaved PCM samples.
class SampleRing {
public:
    void reset(std::size_t capacity) {
        m_samples.assign(capacity, 0);
        m_read = 0;
        m_write = 0;
    }

    [[nodiscard]] std::size_t size() const { return m_write.load() - m_read.load(); }

    std::size_t push(const std::int16_t* samples, std::size_t count) {
        const std::size_t write = m_write.load(std::memory_order_relaxed);
        const std::size_t room =
            m_samples.size() - (write - m_read.load(std::memory_order_acquire));
        const std::size_t n = std::min(count, room);
        for (std::size_t i = 0; i < n; ++i) {
            m_samples[(write + i) % m_samples.size()] = samples[i];
        }
        m_write.store(write + n, std::memory_order_release);
        return n;
    }

    std::size_t pop(std::int16_t* samples, std::size_t count) {
        const std::size_t read = m_read.load(std::memory_order_relaxed);
        const std::size_t available = m_write.load(std::memory_order_acquire) - read;
        const std::size_t n = std::min(count, available);
        for (std::size_t i = 0; i < n; ++i) {
            samples[i] = m_samples[(read + i) % m_samples.size()];
        }
        m_read.store(read + n, std::memory_order_release);
        return n;
    }

private:
    std::vector<std::int16_t> m_samples;
    std::atomic<std::size_t> m_read = 0;
    std::atomic<std::size_t> m_write = 0;
};

// AAudio output pulling from a SampleRing through a TimeStretch. The media time of the frames
// played is the clock, so video and subtitles follow the playback speed.
class AudioOutput {
public:
    AudioOutput()
        : m_read([this](std::int16_t* destination, std::size_t count) {
            return m_ring.pop(destination, count);
        }) {}

    ~AudioOutput() { close(); }

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    bool open(std::int32_t sampleRate, std::int32_t channels) {
        close();
        m_sampleRate = sampleRate;
        m_channels = channels;
        m_consumedFrames = 0;
        m_mediaFrames = 0;
        m_running = false; // A new stream starts stopped.
        m_ring.reset(static_cast<std::size_t>(sampleRate * channels * kAudioBufferSeconds));
        m_stretch.reset(sampleRate, channels);

        AAudioStreamBuilder* builder = nullptr;
        if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) {
            return false;
        }
        AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
        AAudioStreamBuilder_setSampleRate(builder, sampleRate);
        AAudioStreamBuilder_setChannelCount(builder, channels);
        AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_POWER_SAVING);
        AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_MEDIA);
        AAudioStreamBuilder_setContentType(builder, AAUDIO_CONTENT_TYPE_MOVIE);
        AAudioStreamBuilder_setDataCallback(builder, &AudioOutput::render, this);
        const bool ok = AAudioStreamBuilder_openStream(builder, &m_stream) == AAUDIO_OK;
        AAudioStreamBuilder_delete(builder);
        return ok;
    }

    void close() {
        if (m_stream != nullptr) {
            AAudioStream_close(m_stream);
            m_stream = nullptr;
        }
    }

    void setRunning(bool running) {
        if (m_stream == nullptr || running == m_running) {
            return;
        }
        m_running = running;
        if (running) {
            AAudioStream_requestStart(m_stream);
        } else {
            AAudioStream_requestPause(m_stream);
        }
    }

    [[nodiscard]] SampleRing& ring() { return m_ring; }

    [[nodiscard]] bool drained() const { return m_ring.size() == 0; }

    void setSpeed(float speed) { m_speed = speed; }

    // Media time elapsed since the first queued sample, compensated for device latency.
    [[nodiscard]] std::int64_t playedUs() const {
        if (m_stream == nullptr || m_sampleRate == 0) {
            return 0;
        }
        const std::int64_t inDevice =
            AAudioStream_getFramesWritten(m_stream) - AAudioStream_getFramesRead(m_stream);
        // Frames still in the device were stretched at about the current speed.
        const auto frames = std::max<std::int64_t>(
            0, m_consumedFrames - std::llround(static_cast<double>(inDevice) * m_speed));
        return frames * kMicrosPerSecond / m_sampleRate;
    }

    // Called while the stream is stopped, so the callback is not running.
    void resetClock() {
        m_consumedFrames = 0;
        m_mediaFrames = 0;
    }

    [[nodiscard]] std::int32_t sampleRate() const { return m_sampleRate; }

    [[nodiscard]] std::int32_t channels() const { return m_channels; }

private:
    static aaudio_data_callback_result_t render(AAudioStream*, void* user, void* audioData,
                                                std::int32_t frames) {
        auto& self = *static_cast<AudioOutput*>(user);
        // Underruns play silence and leave the clock where it is.
        const bool rendered = guarded("audio", [&] {
            self.m_mediaFrames +=
                self.m_stretch.render(static_cast<std::int16_t*>(audioData),
                                      static_cast<std::size_t>(frames), self.m_speed, self.m_read);
        });
        if (!rendered) {
            std::fill_n(static_cast<std::int16_t*>(audioData),
                        static_cast<std::size_t>(frames) *
                            static_cast<std::size_t>(self.m_channels),
                        std::int16_t{0});
        }
        self.m_consumedFrames = std::llround(self.m_mediaFrames);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    AAudioStream* m_stream = nullptr;
    SampleRing m_ring;
    TimeStretch m_stretch;
    TimeStretch::Read m_read;
    std::atomic<float> m_speed = 1;
    double m_mediaFrames = 0; // Input frames played, owned by the callback.
    std::atomic<std::int64_t> m_consumedFrames = 0;
    std::int32_t m_sampleRate = 0;
    std::int32_t m_channels = 0;
    bool m_running = false;
};

struct CodecDeleter {
    void operator()(AMediaCodec* codec) const {
        AMediaCodec_stop(codec);
        AMediaCodec_delete(codec);
    }
};

struct ExtractorDeleter {
    void operator()(AMediaExtractor* extractor) const { AMediaExtractor_delete(extractor); }
};

struct FormatDeleter {
    void operator()(AMediaFormat* format) const { AMediaFormat_delete(format); }
};

using CodecPtr = std::unique_ptr<AMediaCodec, CodecDeleter>;
using ExtractorPtr = std::unique_ptr<AMediaExtractor, ExtractorDeleter>;
using FormatPtr = std::unique_ptr<AMediaFormat, FormatDeleter>;

// A live segment held in memory, and its demuxer.
struct SegmentFile {
    explicit SegmentFile(std::string bytes)
        : data(std::move(bytes)) {
        AMediaDataSource_setUserdata(source, this);
        AMediaDataSource_setReadAt(
            source, [](void* self, off64_t offset, void* buffer, size_t size) -> ssize_t {
                const std::string& file = static_cast<SegmentFile*>(self)->data;
                if (offset < 0 || static_cast<std::size_t>(offset) >= file.size()) {
                    return 0;
                }
                const std::size_t count =
                    std::min(size, file.size() - static_cast<std::size_t>(offset));
                std::memcpy(buffer, file.data() + offset, count);
                return static_cast<ssize_t>(count);
            });
        AMediaDataSource_setGetSize(source, [](void* self) -> ssize_t {
            return static_cast<ssize_t>(static_cast<SegmentFile*>(self)->data.size());
        });
        AMediaDataSource_setClose(source, [](void*) {});
    }

    ~SegmentFile() {
        extractor.reset(); // Stops using the source before it goes.
        AMediaDataSource_delete(source);
    }

    SegmentFile(const SegmentFile&) = delete;
    SegmentFile& operator=(const SegmentFile&) = delete;

    [[nodiscard]] bool open() const {
        return AMediaExtractor_setDataSourceCustom(extractor.get(), source) == AMEDIA_OK;
    }

    std::string data;
    AMediaDataSource* source = AMediaDataSource_new();
    ExtractorPtr extractor{AMediaExtractor_new()};
};

// A decoded buffer waiting to be consumed.
struct Pending {
    ssize_t index = -1;
    std::int64_t presentationUs = 0;
    std::size_t offset = 0;
    std::size_t remaining = 0;
};

std::int32_t formatInt(AMediaFormat* format, const char* key) {
    std::int32_t value = 0;
    AMediaFormat_getInt32(format, key, &value);
    return value;
}

// Decoder of one elementary stream fed from a demuxer.
struct Track {
    CodecPtr codec;
    AMediaExtractor* extractor = nullptr;
    std::int32_t index = -1;
    bool inputEnded = false; // End-of-stream queued to the codec.
    bool outputEnded = false;
    Pending pending;

    [[nodiscard]] bool active() const { return codec != nullptr; }

    void drop() {
        if (pending.index >= 0) {
            AMediaCodec_releaseOutputBuffer(codec.get(), static_cast<std::size_t>(pending.index),
                                            false);
            pending.index = -1;
        }
    }

    void reset() {
        inputEnded = false;
        outputEnded = false;
    }
};

std::string_view mimeOf(AMediaFormat* format) {
    const char* mime = nullptr;
    AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime);
    return mime != nullptr ? mime : "";
}

// Creates and starts a decoder for `track` of `extractor`, rendering to `window` when given.
bool startDecoder(Track& out, AMediaExtractor* extractor, std::int32_t track,
                  ANativeWindow* window) {
    const FormatPtr format(
        AMediaExtractor_getTrackFormat(extractor, static_cast<std::size_t>(track)));
    const std::string mime(mimeOf(format.get()));
    CodecPtr codec(AMediaCodec_createDecoderByType(mime.c_str()));
    if (!codec ||
        AMediaCodec_configure(codec.get(), format.get(), window, nullptr, 0) != AMEDIA_OK ||
        AMediaCodec_start(codec.get()) != AMEDIA_OK) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "no decoder for %s", mime.c_str());
        return false;
    }
    AMediaExtractor_selectTrack(extractor, static_cast<std::size_t>(track));
    out.codec = std::move(codec);
    out.extractor = extractor;
    out.index = track;
    out.reset();
    return true;
}

// Queues an end-of-stream marker once an input slot frees up.
void signalEnd(Track& track) {
    if (track.inputEnded || !track.active()) {
        return;
    }
    const ssize_t slot = AMediaCodec_dequeueInputBuffer(track.codec.get(), 0);
    if (slot >= 0) {
        AMediaCodec_queueInputBuffer(track.codec.get(), static_cast<std::size_t>(slot), 0, 0, 0,
                                     AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        track.inputEnded = true;
    }
}

// Moves one compressed sample from `extractor` to the decoder owning it; returns whether one
// moved. Several tracks may share an extractor (progressive streams).
bool feed(AMediaExtractor* extractor, std::span<Track* const> tracks) {
    const ssize_t index = AMediaExtractor_getSampleTrackIndex(extractor);
    if (index < 0) {
        for (Track* track : tracks) {
            if (track->extractor == extractor) {
                signalEnd(*track);
            }
        }
        return false;
    }
    const auto owner = std::ranges::find_if(tracks, [&](const Track* track) {
        return track->extractor == extractor && track->index == index && track->active();
    });
    if (owner == tracks.end()) {
        AMediaExtractor_advance(extractor);
        return true;
    }
    AMediaCodec* codec = (*owner)->codec.get();
    const ssize_t slot = AMediaCodec_dequeueInputBuffer(codec, 0);
    if (slot < 0) {
        return false;
    }
    std::size_t capacity = 0;
    std::uint8_t* buffer =
        AMediaCodec_getInputBuffer(codec, static_cast<std::size_t>(slot), &capacity);
    const ssize_t size = AMediaExtractor_readSampleData(extractor, buffer, capacity);
    const std::int64_t time = AMediaExtractor_getSampleTime(extractor);
    AMediaCodec_queueInputBuffer(codec, static_cast<std::size_t>(slot), 0,
                                 static_cast<std::size_t>(std::max<ssize_t>(size, 0)),
                                 static_cast<std::uint64_t>(std::max<std::int64_t>(time, 0)), 0);
    AMediaExtractor_advance(extractor);
    return true;
}

std::optional<std::int32_t> findTrack(AMediaExtractor* extractor, std::string_view kind) {
    const auto count = static_cast<std::int32_t>(AMediaExtractor_getTrackCount(extractor));
    for (std::int32_t track = 0; track < count; ++track) {
        const FormatPtr format(
            AMediaExtractor_getTrackFormat(extractor, static_cast<std::size_t>(track)));
        if (mimeOf(format.get()).starts_with(kind)) {
            return track;
        }
    }
    return std::nullopt;
}

std::int64_t trackDuration(AMediaExtractor* extractor, std::int32_t track) {
    const FormatPtr format(
        AMediaExtractor_getTrackFormat(extractor, static_cast<std::size_t>(track)));
    std::int64_t duration = 0;
    AMediaFormat_getInt64(format.get(), AMEDIAFORMAT_KEY_DURATION, &duration);
    return duration;
}

} // namespace

// A remote file, or the segments of a live stream, and its demuxer.
struct Player::Input {
    Input(Session& owner, std::unique_ptr<HttpSource> data);
    Input(Session& owner, std::unique_ptr<LiveSource> data);
    ~Input();

    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    // Parses the container (a live stream's first segment); blocks on the network.
    bool open();

    // Live: moves on to the next segment, where the tracks of `tracks` demuxed from the current
    // one stay selected. False while that segment has not arrived.
    bool advance(std::span<Track* const> tracks, bool wait = false);

    [[nodiscard]] AMediaExtractor* demuxer() const {
        return segment ? segment->extractor.get() : extractor.get();
    }

    // Live: every sample of the current segment has been read.
    [[nodiscard]] bool exhausted() const {
        return live && AMediaExtractor_getSampleTrackIndex(demuxer()) < 0;
    }

    Session& session;
    std::unique_ptr<HttpSource> source;
    std::unique_ptr<LiveSource> live;
    ExtractorPtr extractor{AMediaExtractor_new()}; // Of `source`.
    std::unique_ptr<SegmentFile> segment;          // Live: the segment being demuxed.
    std::int64_t sequence = -1;                    // Live: number of `segment`.
    std::int64_t startUs = -1;                     // Live: first sample time of `segment`.
};

struct Player::Session {
    Session(innertube::Player video, innertube::Streams chosen, std::size_t initialQuality,
            bool startPaused)
        : info(std::move(video))
        , streams(std::move(chosen))
        , quality(initialQuality)
        , target(initialQuality)
        , playing(initialQuality) {
        paused = startPaused;
        standby = startPaused;
        durationUs = info.details.durationSeconds * kMicrosPerSecond;
        if (info.details.live && !streams.qualities.empty()) {
            const innertube::Format& format = info.formats[streams.qualities.front().video];
            segmentUs = format.segmentSeconds * kMicrosPerSecond;
            dvrUs = format.dvrSeconds * kMicrosPerSecond;
        }
    }

    ~Session() {
        if (AImageReader* image = reader.exchange(nullptr)) {
            AImageReader_delete(image);
        }
    }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void track(Source* source) {
        const std::scoped_lock lock(sourcesMutex);
        if (stopping) {
            source->cancel();
        }
        source->setBriefReadAhead(standby);
        sources.push_back(source);
    }

    // A standby session buffers little until it is promoted.
    void setStandby(bool value) {
        const std::scoped_lock lock(sourcesMutex);
        standby = value;
        for (Source* source : sources) {
            source->setBriefReadAhead(value);
        }
    }

    void untrack(Source* source) {
        const std::scoped_lock lock(sourcesMutex);
        std::erase(sources, source);
    }

    void stop() {
        {
            const std::scoped_lock lock(sourcesMutex);
            stopping = true;
            for (Source* source : sources) {
                source->cancel();
            }
        }
        {
            const std::scoped_lock lock(switchMutex);
            stopRequested = true;
        }
        switchChanged.notify_all();
    }

    // Media time of the audio clock, or of the pending start position before it runs.
    [[nodiscard]] std::int64_t clockUs() const {
        const std::int64_t base = clockBaseUs;
        return base < 0 ? anchorUs.load() : base + audio.playedUs();
    }

    const innertube::Player info;
    const innertube::Streams streams;
    const std::size_t quality; // Quality the session opened with.
    AudioOutput audio;
    std::atomic<PlayerState> state = PlayerState::Loading;
    std::atomic<bool> buffering = false;
    std::atomic<bool> paused;
    std::atomic<std::int64_t> seekRequestUs = -1;
    std::atomic<std::int64_t> durationUs = 0;
    std::atomic<AImageReader*> reader = nullptr; // Created by the decoder, deleted with us.
    std::atomic<int> videoWidth = 0;
    std::atomic<int> videoHeight = 0;
    std::atomic<std::int64_t> clockBaseUs = -1; // Timestamp of the first sample after a seek.
    std::atomic<std::int64_t> anchorUs = 0;     // Position shown while the clock is stopped.
    std::atomic<std::size_t> target;            // Quality selected most recently.
    std::atomic<std::size_t> playing;           // Quality of the video stream being decoded.
    std::atomic<bool> stopRequested = false;
    std::int64_t segmentUs = 0; // Live: media time of a segment.
    std::int64_t dvrUs = 0;     // Live: how far behind the edge playback can go.
    // Bumped by every live seek, so a quality switch prepared before it is not swapped in.
    std::atomic<std::uint64_t> seekGeneration = 0;

    // Live: the segment audio is demuxed from, and its start: where a quality switch begins.
    void setLivePosition(std::int64_t sequence, std::int64_t startUs) {
        const std::scoped_lock lock(liveMutex);
        liveSequence = sequence;
        liveSegmentUs = startUs;
    }

    [[nodiscard]] std::pair<std::int64_t, std::int64_t> livePosition() const {
        const std::scoped_lock lock(liveMutex);
        return {liveSequence, liveSegmentUs};
    }

    // Live: media time of segment 0, from the audio's current segment; nullopt until known.
    // Segment n starts at offset + n * segmentUs, give or take half a second.
    [[nodiscard]] std::optional<std::int64_t> liveOffsetUs() const {
        const auto [sequence, startUs] = livePosition();
        if (segmentUs <= 0 || sequence < 0 || startUs < 0) {
            return std::nullopt;
        }
        return startUs - sequence * segmentUs;
    }

    // Live: the edge segment's sequence number now; it stops moving once the broadcast ended.
    [[nodiscard]] std::optional<std::int64_t> edgeNow() const {
        const std::scoped_lock lock(liveMutex);
        if (segmentUs <= 0 || edgeSequence < 0) {
            return std::nullopt;
        }
        return edgeAt(steadyMicros());
    }

    void learnEdge(std::int64_t edge) {
        if (edge >= 0) {
            const std::scoped_lock lock(liveMutex);
            edgeLearnedUs = steadyMicros();
            edgeSequence = edge;
            edgeFrozen = false;
        }
    }

    // The broadcast is over: the edge is where it was last.
    void freezeEdge() {
        const std::scoped_lock lock(liveMutex);
        if (!edgeFrozen && edgeSequence >= 0 && segmentUs > 0) {
            edgeSequence = edgeAt(steadyMicros());
            edgeFrozen = true;
        }
    }

    [[nodiscard]] std::int64_t edgeAt(std::int64_t nowUs) const { // Under liveMutex.
        return edgeFrozen ? edgeSequence : edgeSequence + (nowUs - edgeLearnedUs) / segmentUs;
    }

    mutable std::mutex liveMutex;
    std::int64_t liveSequence = -1;  // Under liveMutex.
    std::int64_t liveSegmentUs = -1; // Under liveMutex.
    // The edge segment when last learned, and when (steady clock, µs); it moves on by one segment
    // every segmentUs. Under liveMutex.
    std::int64_t edgeSequence = -1;
    std::int64_t edgeLearnedUs = 0;
    bool edgeFrozen = false;

    static std::int64_t steadyMicros() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    std::mutex sourcesMutex;
    std::vector<Source*> sources; // Every open source, cancelled on stop.
    bool standby = false;         // Under sourcesMutex.
    bool stopping = false;

    // Quality switches travel UI thread -> loader thread (opens the new stream) -> decoder.
    std::mutex switchMutex;
    std::condition_variable switchChanged;
    std::optional<std::size_t> requested;
    std::unique_ptr<Input> prepared;
    std::size_t preparedQuality = 0;
    std::int64_t preparedAtUs = 0;
    std::vector<std::unique_ptr<Input>> retired; // Destroyed by the loader: joining blocks.

    std::thread decoder;
    std::thread loader;
};

Player::Input::Input(Session& owner, std::unique_ptr<HttpSource> data)
    : session(owner)
    , source(std::move(data)) {
    session.track(source.get());
}

Player::Input::Input(Session& owner, std::unique_ptr<LiveSource> data)
    : session(owner)
    , live(std::move(data)) {
    session.track(live.get());
}

Player::Input::~Input() {
    extractor.reset(); // Stops using the source before it goes.
    segment.reset();
    session.untrack(source ? static_cast<Source*>(source.get()) : live.get());
}

bool Player::Input::open() {
    if (live) {
        return advance({}, true) && segment;
    }
    return AMediaExtractor_setDataSourceCustom(extractor.get(), source->dataSource()) == AMEDIA_OK;
}

bool Player::Input::advance(std::span<Track* const> tracks, bool wait) {
    auto next = live->take(wait);
    if (!next) {
        return false;
    }
    auto file = std::make_unique<SegmentFile>(std::move(next->data));
    if (!file->open()) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "live: cannot demux segment %lld",
                            static_cast<long long>(next->sequence));
        return true; // Skipped; the next one follows.
    }
    AMediaExtractor* previous = demuxer();
    for (Track* track : tracks) {
        if (track->extractor == previous && track->active()) {
            AMediaExtractor_selectTrack(file->extractor.get(),
                                        static_cast<std::size_t>(track->index));
            track->extractor = file->extractor.get();
        }
    }
    segment = std::move(file);
    sequence = next->sequence;
    startUs = AMediaExtractor_getSampleTime(segment->extractor.get());
    return true;
}

Player::Player(const http::Client& http, std::function<void()> onFrame)
    : m_http(http)
    , m_onFrame(std::move(onFrame)) {}

Player::~Player() {
    close();
    closeStandby();
    // Reapers use this player until their decoder has stopped.
    std::unique_lock lock(m_reapMutex);
    m_reaped.wait(lock, [&] { return m_reaping == 0; });
}

void Player::open(innertube::Player info, int screenLines) {
    close();
    m_screenLines = screenLines;
    m_session = start(std::move(info), false);
    m_failed = !m_session;
}

void Player::preload(innertube::Player info, int screenLines) {
    stopSession(m_standby);
    m_screenLines = screenLines;
    m_standby = start(std::move(info), true);
}

bool Player::promote(std::string_view videoId) {
    if (!m_standby || m_standby->info.details.id != videoId) {
        return false;
    }
    close();
    m_session = std::move(m_standby);
    m_session->audio.setSpeed(m_session->info.details.live ? 1 : m_speed.load());
    m_session->setStandby(false);
    m_session->paused = false;
    m_failed = false;
    return true;
}

std::size_t Player::preferredQuality(const innertube::Streams& streams,
                                     const innertube::Player& info) const {
    if (m_preferredLines == 0) {
        return innertube::autoQuality(
            streams, info, {.maxLines = m_screenLines, .bandwidthBps = m_network.bandwidthBps()});
    }
    const auto& qualities = streams.qualities;
    const auto fits = std::ranges::find_if(
        qualities, [&](const innertube::Quality& q) { return q.lines <= m_preferredLines; });
    return fits != qualities.end() ? static_cast<std::size_t>(fits - qualities.begin())
                                   : qualities.size() - 1;
}

std::unique_ptr<Player::Session> Player::start(innertube::Player info, bool paused) {
    auto streams = innertube::selectStreams(info, {.maxLines = kMaxDecodableLines});
    if (!streams.audio && streams.qualities.size() > 1) {
        streams.qualities.resize(1); // Switching muxed streams would restart audio too.
    }
    if (streams.qualities.empty()) {
        return nullptr;
    }
    const std::size_t quality = preferredQuality(streams, info);
    auto session = std::make_unique<Session>(std::move(info), std::move(streams), quality, paused);
    Session& running = *session;
    running.audio.setSpeed(paused || running.info.details.live ? 1 : m_speed.load());
    running.decoder = std::thread([this, &running] {
        if (!guarded("decoder", [&] { decodeLoop(running); })) {
            running.state = PlayerState::Failed;
        }
    });
    if (running.streams.audio) {
        running.loader =
            std::thread([this, &running] { guarded("loader", [&] { loaderLoop(running); }); });
    }
    return session;
}

void Player::stopSession(std::unique_ptr<Session>& session) {
    if (!session) {
        return;
    }
    session->stop();
    {
        const std::scoped_lock lock(m_reapMutex);
        ++m_reaping;
        m_reaped.notify_all(); // A decoder of this session waiting for reapers gives up.
    }
    std::thread([this, dying = std::move(session)]() mutable {
        guarded("reaper", [&] {
            dying->decoder.join();
            if (dying->loader.joinable()) {
                dying->loader.join();
            }
            dying.reset();
        });
        // Notified under the lock: ~Player may destroy the condition right after.
        const std::scoped_lock lock(m_reapMutex);
        --m_reaping;
        m_reaped.notify_all();
    }).detach();
}

void Player::close() {
    stopSession(m_session);
    m_failed = false;
}

void Player::closeStandby() {
    stopSession(m_standby);
}

void Player::setPaused(bool paused) {
    if (m_session) {
        m_session->paused = paused;
    }
}

void Player::setSpeed(float speed) {
    m_speed = speed;
    if (m_session) {
        m_session->audio.setSpeed(m_session->info.details.live ? 1 : speed);
    }
}

void Player::seek(std::int64_t positionUs) {
    if (!m_session) {
        return;
    }
    std::int64_t target = positionUs;
    if (m_session->info.details.live) {
        const auto window = liveWindow();
        if (!window) {
            return;
        }
        target = std::clamp(positionUs, window->startUs, window->edgeUs);
    } else {
        target = std::clamp<std::int64_t>(positionUs, 0, m_session->durationUs);
    }
    m_session->seekRequestUs = target;
    m_session->anchorUs = target;
}

std::span<const innertube::Quality> Player::qualities() const {
    if (!m_session) {
        return {};
    }
    return m_session->streams.qualities;
}

PlayerState Player::state() const {
    if (!m_session) {
        return m_failed ? PlayerState::Failed : PlayerState::Idle;
    }
    return m_session->state;
}

std::int64_t Player::durationUs() const {
    return m_session ? m_session->durationUs.load() : 0;
}

AImageReader* Player::imageReader() const {
    return m_session ? m_session->reader.load() : nullptr;
}

std::pair<int, int> Player::videoSize() const {
    return m_session ? std::pair{m_session->videoWidth.load(), m_session->videoHeight.load()}
                     : std::pair{0, 0};
}

const innertube::VideoDetails* Player::details() const {
    return m_session ? &m_session->info.details : nullptr;
}

std::optional<std::size_t> Player::currentQuality() const {
    if (!m_session) {
        return std::nullopt;
    }
    return m_session->target.load();
}

void Player::selectQuality(std::optional<std::size_t> quality) {
    if (!m_session) {
        return;
    }
    const auto& streams = m_session->streams;
    if (quality && *quality >= streams.qualities.size()) {
        return;
    }
    m_preferredLines = quality ? streams.qualities[*quality].lines : 0;
    if (!streams.audio) {
        return;
    }
    const std::size_t index = quality ? *quality : preferredQuality(streams, m_session->info);
    if (index == m_session->target) {
        return;
    }
    m_session->target = index;
    {
        const std::scoped_lock lock(m_session->switchMutex);
        m_session->requested = index;
    }
    m_session->switchChanged.notify_all();
}

bool Player::buffering() const {
    if (!m_session) {
        return false;
    }
    if (m_session->buffering) {
        return true;
    }
    // The decoder blocks inside the demuxer while data is missing, so it cannot report this.
    return m_session->state == PlayerState::Playing && !m_session->paused && m_network.starved() &&
           m_session->audio.drained();
}

std::int64_t Player::positionUs() const {
    if (!m_session) {
        return 0;
    }
    return m_session->clockUs();
}

bool Player::live() const {
    return m_session && m_session->info.details.live;
}

std::optional<Player::LiveWindow> Player::liveWindow() const {
    if (!live()) {
        return std::nullopt;
    }
    const Session& session = *m_session;
    const auto offset = session.liveOffsetUs();
    const auto edge = session.edgeNow();
    if (!offset || !edge) {
        return std::nullopt;
    }
    // The edge is the end of its segment, as far as playback can go.
    const std::int64_t edgeUs = *offset + (*edge + 1) * session.segmentUs;
    // A margin from the oldest segment, which the server may drop at any moment.
    constexpr std::int64_t kDvrMarginSegments = 2;
    const std::int64_t startUs =
        std::max(*offset, edgeUs - session.dvrUs + kDvrMarginSegments * session.segmentUs);
    return LiveWindow{.startUs = std::min(startUs, edgeUs), .edgeUs = edgeUs};
}

void Player::setVideoHidden(bool hidden) {
    m_videoHidden = hidden;
    if (!hidden) {
        m_repaint = true; // The last frame shown is from when the app was left.
    }
}

std::unique_ptr<Player::Input> Player::openInput(const innertube::Format& format, Session& session,
                                                 std::int64_t firstSegment, bool seeking) {
    auto input =
        format.segmented()
            ? std::make_unique<Input>(
                  session, std::make_unique<LiveSource>(m_http, format.url, firstSegment,
                                                        seeking ? kLiveSeekMisses : kLiveMaxMisses))
            : std::make_unique<Input>(
                  session, std::make_unique<HttpSource>(m_http, format.url, format.contentLength,
                                                        format.bitrate, m_network));
    if (!input->open()) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "cannot demux itag %d", format.itag);
        return nullptr;
    }
    return input;
}

void Player::loaderLoop(Session& session) {
    while (true) {
        std::optional<std::size_t> quality;
        std::vector<std::unique_ptr<Input>> retired;
        {
            std::unique_lock lock(session.switchMutex);
            session.switchChanged.wait(lock, [&] {
                return session.stopRequested || session.requested || !session.retired.empty();
            });
            if (session.stopRequested) {
                return; // Pending inputs die with the session.
            }
            retired.swap(session.retired);
            quality = std::exchange(session.requested, std::nullopt);
        }
        retired.clear();
        if (!quality) {
            continue;
        }

        const innertube::Format& format =
            session.info.formats[session.streams.qualities[*quality].video];
        const std::int64_t positionUs = session.clockUs();
        const std::uint64_t generation = session.seekGeneration;
        std::int64_t firstSegment = -1;
        if (format.segmented()) {
            // The segment holding the position: the audio's, or the one before when the audio
            // has already moved past the position.
            const auto [sequence, startUs] = session.livePosition();
            firstSegment = sequence >= 0 && positionUs < startUs ? sequence - 1 : sequence;
        }
        auto input = openInput(format, session, firstSegment);
        const auto track =
            input ? findTrack(input->demuxer(), "video/") : std::optional<std::int32_t>{};
        if (!track) {
            const std::scoped_lock lock(session.switchMutex);
            if (!session.requested) {
                session.target = session.playing.load(); // Report what still plays.
            }
            continue;
        }
        AMediaExtractor* extractor = input->demuxer();
        AMediaExtractor_selectTrack(extractor, static_cast<std::size_t>(*track));
        if (!format.segmented()) {
            AMediaExtractor_seekTo(extractor, positionUs, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
        }
        (void)AMediaExtractor_getSampleTime(extractor); // Loads the first fragment here.

        const std::scoped_lock lock(session.switchMutex);
        if (session.requested) {
            session.retired.push_back(std::move(input)); // Superseded while loading.
        } else if (generation != session.seekGeneration) {
            session.retired.push_back(std::move(input)); // A live seek moved away: again.
            session.requested = *quality;
        } else {
            session.retired.push_back(std::move(session.prepared));
            session.prepared = std::move(input);
            session.preparedQuality = *quality;
            session.preparedAtUs = positionUs;
        }
    }
}

void Player::decodeLoop(Session& session) {
    // The decoder also feeds the sound: a late wake-up is an audible gap
    // (Process.THREAD_PRIORITY_AUDIO).
    constexpr int kAudioPriority = -16;
    setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), kAudioPriority);
    {
        // Hardware decoders are few: the ones of videos being closed go first.
        std::unique_lock lock(m_reapMutex);
        m_reaped.wait(lock, [&] { return m_reaping == 0 || session.stopRequested; });
    }
    if (session.stopRequested) {
        session.state = PlayerState::Idle;
        return;
    }
    const innertube::Quality& initial = session.streams.qualities[session.quality];
    const innertube::Format& videoFormat = session.info.formats[initial.video];
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "open %s itag %d %s", initial.label.c_str(),
                        videoFormat.itag, videoFormat.mimeType.c_str());

    std::unique_ptr<Input> main = openInput(videoFormat, session);
    std::unique_ptr<Input> audioInput;
    if (main && session.streams.audio) {
        const innertube::Format& audioFormat = session.info.formats[*session.streams.audio];
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "audio itag %d %s", audioFormat.itag,
                            audioFormat.mimeType.c_str());
        // A live audio stream starts at the video's segment, so both cover the same time.
        audioInput = openInput(audioFormat, session, main->live ? main->sequence : -1);
    }
    if (!main || (session.streams.audio && !audioInput)) {
        session.state = session.stopRequested ? PlayerState::Idle : PlayerState::Failed;
        return;
    }
    AMediaExtractor* audioExtractor = audioInput ? audioInput->demuxer() : main->demuxer();

    Track video;
    Track audio;
    std::int64_t duration = 0;
    if (const auto track = findTrack(main->demuxer(), "video/")) {
        const FormatPtr format(
            AMediaExtractor_getTrackFormat(main->demuxer(), static_cast<std::size_t>(*track)));
        AImageReader* reader = nullptr;
        session.videoWidth = formatInt(format.get(), AMEDIAFORMAT_KEY_WIDTH);
        session.videoHeight = formatInt(format.get(), AMEDIAFORMAT_KEY_HEIGHT);
        AImageReader_newWithUsage(session.videoWidth, session.videoHeight, AIMAGE_FORMAT_PRIVATE,
                                  AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE, kReaderMaxImages,
                                  &reader);
        if (reader != nullptr) {
            AImageReader_ImageListener listener{
                .context = this,
                .onImageAvailable = [](void* self,
                                       AImageReader*) { static_cast<Player*>(self)->m_onFrame(); },
            };
            AImageReader_setImageListener(reader, &listener);
            session.reader = reader;
            ANativeWindow* window = nullptr;
            AImageReader_getWindow(reader, &window);
            startDecoder(video, main->demuxer(), *track, window);
            duration = trackDuration(main->demuxer(), *track);
        }
    }
    if (const auto track = findTrack(audioExtractor, "audio/");
        track && startDecoder(audio, audioExtractor, *track, nullptr)) {
        const FormatPtr format(
            AMediaExtractor_getTrackFormat(audioExtractor, static_cast<std::size_t>(*track)));
        session.audio.open(formatInt(format.get(), AMEDIAFORMAT_KEY_SAMPLE_RATE),
                           formatInt(format.get(), AMEDIAFORMAT_KEY_CHANNEL_COUNT));
        duration = std::max(duration, trackDuration(audioExtractor, *track));
        if (audioInput && audioInput->live) {
            session.setLivePosition(audioInput->sequence,
                                    AMediaExtractor_getSampleTime(audioExtractor));
            session.learnEdge(main->live->edge());
        }
    }
    if (!audio.active()) {
        session.state = PlayerState::Failed; // Audio is the clock.
        return;
    }
    if (duration > 0 && !session.info.details.live) {
        session.durationUs = duration;
    }

    const std::array<Track*, 2> tracks{&video, &audio};
    // A live stream seeks by starting over at the segment holding `targetUs`, or near the edge;
    // `targetUs` becomes where playback resumes. False when that segment cannot be had.
    const auto restartLive = [&](std::int64_t& targetUs) {
        const auto offset = session.liveOffsetUs();
        const auto edge = session.edgeNow();
        if (!offset || !edge || !audioInput) {
            return false;
        }
        // Close enough to the edge plays from where a fresh start would: a few segments behind.
        std::int64_t first = (targetUs - *offset) / session.segmentUs;
        const bool toEdge = first >= *edge - kLiveStartBehindSegments;
        if (toEdge) {
            first = -1;
        }
        const innertube::Format& videoFormat =
            session.info.formats[session.streams.qualities[session.playing].video];
        auto videoStream = openInput(videoFormat, session, first, true);
        auto audioStream = videoStream ? openInput(session.info.formats[*session.streams.audio],
                                                   session, videoStream->sequence, true)
                                       : nullptr;
        if (!audioStream) {
            return false;
        }
        for (Track* track : tracks) {
            Input& input = track->extractor == main->demuxer() ? *videoStream : *audioStream;
            if (track->active()) {
                AMediaExtractor_selectTrack(input.demuxer(),
                                            static_cast<std::size_t>(track->index));
                track->extractor = input.demuxer();
                input.startUs = AMediaExtractor_getSampleTime(input.demuxer());
            }
        }
        {
            // Their sources join their fetch threads when destroyed: the loader does that.
            const std::scoped_lock lock(session.switchMutex);
            session.retired.push_back(std::move(main));
            session.retired.push_back(std::move(audioInput));
            // A quality switch opened before the seek is at the old time: open it again here.
            ++session.seekGeneration;
            if (session.prepared) {
                session.retired.push_back(std::move(session.prepared));
                if (!session.requested) {
                    session.requested = session.target.load();
                }
            }
        }
        session.switchChanged.notify_all();
        main = std::move(videoStream);
        audioInput = std::move(audioStream);
        session.setLivePosition(audioInput->sequence, audioInput->startUs);
        if (toEdge) {
            session.learnEdge(main->live->edge());
            targetUs = std::max<std::int64_t>(audioInput->startUs, 0); // Nothing to skip.
        }
        return true;
    };
    // Moves one sample of `input` to its decoder. A live input first moves on to its next
    // segment once the current one is used up, and ends only with the broadcast.
    const auto pump = [&](Input& input) {
        if (input.exhausted()) {
            if (input.advance(tracks)) {
                if (&input == audioInput.get()) {
                    session.setLivePosition(input.sequence, input.startUs);
                }
                return true;
            }
            if (!input.live->ended()) {
                return false; // The next segment is still downloading.
            }
            session.freezeEdge();
        }
        return feed(input.demuxer(), tracks);
    };
    std::int64_t audioDiscardUs = 0;
    std::int64_t videoDiscardUs = 0;
    bool firstFrameShown = false;

    while (!session.stopRequested) {
        bool progressed = false;

        if (std::int64_t target = session.seekRequestUs.exchange(-1); target >= 0) {
            if (main->live) {
                // A live stream seeks by starting over at the segment holding the target.
                if (!restartLive(target)) {
                    continue; // Unreachable segment: playback goes on where it was.
                }
            } else {
                AMediaExtractor_seekTo(main->demuxer(), target, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
                if (audioInput) {
                    AMediaExtractor_seekTo(audioInput->demuxer(), target,
                                           AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
                }
            }
            for (Track* track : tracks) {
                if (track->active()) {
                    track->drop();
                    AMediaCodec_flush(track->codec.get());
                    track->reset();
                }
            }
            // Reopening the stream is the only race-free way to discard queued samples.
            session.audio.open(session.audio.sampleRate(), session.audio.channels());
            session.anchorUs = target;
            session.clockBaseUs = -1;
            audioDiscardUs = target;
            videoDiscardUs = target;
            firstFrameShown = false;
        }

        // Swap in a video stream of another quality, opened by the loader at about this time.
        std::unique_ptr<Input> prepared;
        std::size_t preparedQuality = 0;
        std::int64_t preparedAtUs = 0;
        {
            const std::scoped_lock lock(session.switchMutex);
            if (session.prepared) {
                prepared = std::move(session.prepared);
                preparedQuality = session.preparedQuality;
                preparedAtUs = session.preparedAtUs;
            }
        }
        if (prepared && video.active()) {
            AMediaExtractor* extractor = prepared->demuxer();
            const auto track = findTrack(extractor, "video/");
            ANativeWindow* window = nullptr;
            AImageReader_getWindow(session.reader, &window);
            video.drop();
            video.codec.reset(); // Releases the window for the new decoder.
            if (track && startDecoder(video, extractor, *track, window)) {
                const std::int64_t nowUs = session.clockUs();
                if (std::abs(nowUs - preparedAtUs) > kSwitchResyncUs) {
                    AMediaExtractor_seekTo(extractor, nowUs, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
                }
                videoDiscardUs = nowUs;
                firstFrameShown = false;
                {
                    const std::scoped_lock lock(session.switchMutex);
                    session.retired.push_back(std::move(main));
                }
                session.switchChanged.notify_all();
                main = std::move(prepared);
                session.playing = preparedQuality;
                const innertube::Quality& quality = session.streams.qualities[preparedQuality];
                __android_log_print(ANDROID_LOG_INFO, kLogTag, "switched to %s itag %d at %lld ms",
                                    quality.label.c_str(), session.info.formats[quality.video].itag,
                                    static_cast<long long>(nowUs / 1000));
            } else {
                // Keep the old stream: reopen its decoder where it was.
                const auto oldTrack = findTrack(main->demuxer(), "video/");
                if (oldTrack && startDecoder(video, main->demuxer(), *oldTrack, window)) {
                    AMediaExtractor_seekTo(main->demuxer(), session.clockUs(),
                                           AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
                }
                const std::scoped_lock lock(session.switchMutex);
                session.retired.push_back(std::move(prepared));
                if (!session.requested) {
                    session.target = session.playing.load();
                }
            }
            progressed = true;
        }

        if (m_repaint.exchange(false)) {
            firstFrameShown = false; // The next frame is shown at once, paused or not.
        }
        const bool paused = session.paused;
        session.audio.setRunning(!paused && session.clockBaseUs >= 0);

        // Feed compressed samples to the decoders.
        if (main) {
            progressed |= pump(*main);
        }
        if (audioInput) {
            progressed |= pump(*audioInput);
        }

        // Move decoded PCM into the output ring.
        AMediaCodec* audioCodec = audio.codec.get();
        if (audio.pending.index < 0 && !audio.outputEnded) {
            AMediaCodecBufferInfo info{};
            const ssize_t index = AMediaCodec_dequeueOutputBuffer(audioCodec, &info, 0);
            if (index >= 0) {
                if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
                    audio.outputEnded = true;
                }
                if (info.size <= 0 || info.presentationTimeUs < audioDiscardUs) {
                    AMediaCodec_releaseOutputBuffer(audioCodec, static_cast<std::size_t>(index),
                                                    false);
                } else {
                    audio.pending = {.index = index,
                                     .presentationUs = info.presentationTimeUs,
                                     .offset = static_cast<std::size_t>(info.offset),
                                     .remaining = static_cast<std::size_t>(info.size)};
                    if (session.clockBaseUs < 0) {
                        session.audio.resetClock();
                        session.clockBaseUs = info.presentationTimeUs;
                    }
                }
                progressed = true;
            } else if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                const FormatPtr format(AMediaCodec_getOutputFormat(audioCodec));
                const auto rate = formatInt(format.get(), AMEDIAFORMAT_KEY_SAMPLE_RATE);
                const auto channels = formatInt(format.get(), AMEDIAFORMAT_KEY_CHANNEL_COUNT);
                if (rate != session.audio.sampleRate() || channels != session.audio.channels()) {
                    session.audio.open(rate, channels);
                }
            }
        }
        if (audio.pending.index >= 0) {
            std::size_t capacity = 0;
            const std::uint8_t* data = AMediaCodec_getOutputBuffer(
                audioCodec, static_cast<std::size_t>(audio.pending.index), &capacity);
            const std::size_t pushed = session.audio.ring().push(
                reinterpret_cast<const std::int16_t*>(data + audio.pending.offset),
                audio.pending.remaining / sizeof(std::int16_t));
            audio.pending.offset += pushed * sizeof(std::int16_t);
            audio.pending.remaining -= pushed * sizeof(std::int16_t);
            if (audio.pending.remaining == 0) {
                audio.drop();
            }
            progressed |= pushed > 0;
        }

        // Present video frames against the audio clock.
        AMediaCodec* videoCodec = video.codec.get();
        if (video.active() && video.pending.index < 0 && !video.outputEnded) {
            AMediaCodecBufferInfo info{};
            const ssize_t index = AMediaCodec_dequeueOutputBuffer(videoCodec, &info, 0);
            if (index >= 0) {
                if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
                    video.outputEnded = true;
                    AMediaCodec_releaseOutputBuffer(videoCodec, static_cast<std::size_t>(index),
                                                    false);
                } else {
                    video.pending = {.index = index, .presentationUs = info.presentationTimeUs};
                }
                progressed = true;
            } else if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                const FormatPtr format(AMediaCodec_getOutputFormat(videoCodec));
                __android_log_print(ANDROID_LOG_INFO, kLogTag, "decoding %dx%d",
                                    formatInt(format.get(), AMEDIAFORMAT_KEY_WIDTH),
                                    formatInt(format.get(), AMEDIAFORMAT_KEY_HEIGHT));
            }
        }
        if (video.pending.index >= 0) {
            const auto index = static_cast<std::size_t>(video.pending.index);
            if (video.pending.presentationUs < videoDiscardUs) {
                video.drop();
                progressed = true;
            } else if (!firstFrameShown) {
                AMediaCodec_releaseOutputBuffer(videoCodec, index, !m_videoHidden); // Poster.
                video.pending.index = -1;
                firstFrameShown = true;
            } else if (session.clockBaseUs >= 0) {
                const std::int64_t clock = session.clockUs();
                if (video.pending.presentationUs <= clock + kRenderAheadUs) {
                    const bool late = video.pending.presentationUs < clock - kLateDropUs;
                    AMediaCodec_releaseOutputBuffer(videoCodec, index, !late && !m_videoHidden);
                    video.pending.index = -1;
                    progressed = true;
                }
            }
        }

        const bool videoDone = !video.active() || (video.outputEnded && video.pending.index < 0);
        const bool ended =
            audio.outputEnded && audio.pending.index < 0 && session.audio.drained() && videoDone;
        session.buffering = !paused && !ended && !audio.inputEnded && session.audio.drained();
        if (ended) {
            session.state = PlayerState::Ended;
        } else if (session.clockBaseUs >= 0) {
            session.state = paused ? PlayerState::Paused : PlayerState::Playing;
        }

        if (!progressed) {
            std::this_thread::sleep_for(paused || ended ? kPausedWait : kIdleWait);
        }
    }

    for (Track* track : tracks) {
        if (track->active()) {
            track->drop();
        }
    }
    session.audio.close();
}

} // namespace sloptube::app::media
