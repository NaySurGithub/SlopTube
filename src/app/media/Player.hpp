#pragma once

#include "app/media/HttpSource.hpp"
#include "core/innertube/Streams.hpp"
#include "core/innertube/Types.hpp"

#include <media/NdkImageReader.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <utility>

namespace sloptube::http {
class Client;
}

namespace sloptube::app::media {

enum class PlayerState : std::uint8_t {
    Idle,
    Loading,
    Playing,
    Paused,
    Ended,
    Failed,
};

// Hardware-decoded playback of either adaptive streams (separate video and audio files, or the
// segments of a live stream) or a progressive one (muxed). Video frames are delivered to an
// AImageReader for zero-copy GPU sampling; audio drives the clock. Every method is called from the
// UI thread.
class Player {
public:
    // `onFrame` runs on an arbitrary thread whenever a new video frame becomes available.
    Player(const http::Client& http, std::function<void()> onFrame);
    ~Player();

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // Plays the best streams of `info`. Auto never picks a resolution above `screenLines`, the
    // short side of the display in pixels.
    void open(innertube::Player info, int screenLines);
    // Callers must have released every AImage acquired from imageReader() beforehand.
    void close();

    // Opens `info` on a standby session, paused on its first frame, replacing any earlier one;
    // promote() then starts it at once. One standby at most.
    void preload(innertube::Player info, int screenLines);
    // Makes the standby session of `videoId` the current one, closing the current one (same
    // precondition as close()). False when no such session was preloaded.
    bool promote(std::string_view videoId);
    void closeStandby();

    void setPaused(bool paused);
    // Playback rate, pitch preserved: 1 is normal, 2 twice as fast. Carries over to later videos;
    // live streams always play at 1.
    void setSpeed(float speed);

    [[nodiscard]] float speed() const { return m_speed; }

    // A live stream seeks within liveWindow(); at its edge, it goes back to playing live.
    void seek(std::int64_t positionUs);

    // Qualities of the open video, best first.
    [[nodiscard]] std::span<const innertube::Quality> qualities() const;

    // Quality being played (or loaded); nullopt before open().
    [[nodiscard]] std::optional<std::size_t> currentQuality() const;

    [[nodiscard]] bool autoQuality() const { return m_preferredLines == 0; }

    // Switches quality while keeping the position; nullopt selects Auto. The choice carries over
    // to later videos as a resolution.
    void selectQuality(std::optional<std::size_t> quality);

    [[nodiscard]] PlayerState state() const;

    [[nodiscard]] bool buffering() const;
    [[nodiscard]] std::int64_t positionUs() const;

    [[nodiscard]] std::int64_t durationUs() const;

    // The open video is broadcasting now: it has no duration; liveWindow() is where it can seek.
    [[nodiscard]] bool live() const;

    // Media times a live stream can play, from the oldest kept to the edge, in the clock of
    // positionUs(); nullopt until known.
    struct LiveWindow {
        std::int64_t startUs = 0;
        std::int64_t edgeUs = 0;
    };

    [[nodiscard]] std::optional<LiveWindow> liveWindow() const;

    // Decodes without drawing while the app is not on screen: sound and the clock go on, and the
    // decoder never waits for frames nobody takes.
    void setVideoHidden(bool hidden);

    [[nodiscard]] AImageReader* imageReader() const;

    // Size of the video frames in pixels; zero before the first is decoded.
    [[nodiscard]] std::pair<int, int> videoSize() const;

    // Details of the open video; null when none is.
    [[nodiscard]] const innertube::VideoDetails* details() const;

    [[nodiscard]] std::int64_t bandwidthBps() const { return m_network.bandwidthBps(); }

private:
    struct Input;
    struct Session;

    [[nodiscard]] std::size_t preferredQuality(const innertube::Streams& streams,
                                               const innertube::Player& info) const;
    [[nodiscard]] std::unique_ptr<Session> start(innertube::Player info, bool paused);
    static void stopSession(std::unique_ptr<Session>& session);
    void decodeLoop(Session& session);
    void loaderLoop(Session& session);
    // `firstSegment` is where a live stream starts; negative for near the live edge. A live seek
    // gives up quickly on segments that are gone (`seeking`).
    [[nodiscard]] std::unique_ptr<Input> openInput(const innertube::Format& format,
                                                   Session& session, std::int64_t firstSegment = -1,
                                                   bool seeking = false);

    const http::Client& m_http;
    std::function<void()> m_onFrame;
    NetworkStats m_network; // Outlives sessions so Auto benefits from earlier measurements.

    int m_screenLines = 0;
    int m_preferredLines = 0; // Resolution chosen by the user; 0 for Auto.

    std::atomic<float> m_speed = 1;
    std::atomic<bool> m_videoHidden = false;
    std::atomic<bool> m_repaint = false; // Show the next frame at once: the picture is stale.
    bool m_failed = false;               // The last open() found nothing playable.

    std::unique_ptr<Session> m_session;
    std::unique_ptr<Session> m_standby;
};

} // namespace sloptube::app::media
