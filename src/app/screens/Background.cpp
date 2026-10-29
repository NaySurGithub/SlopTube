// Playback beyond the app's own screen: the media session (notification, lock screen, headset
// and Bluetooth controls), background playback, picture-in-picture, and autoplay.

#include "app/App.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"

namespace sloptube::app {
namespace {

// The official app counts down this long before playing the next video.
constexpr double kAutoplayDelay = 5;
// The media session extrapolates the position itself; it is told again when it drifts by this.
constexpr std::int64_t kPositionDriftMs = 2000;
// While playing unseen, the event loop wakes this often to notice the end of the video.
constexpr int kBackgroundWakeMs = 500;
constexpr int kPlayingWakeMs = 250;
// The text cursor blinks; nothing else needs redrawing while typing until a key is pressed.
constexpr int kCaretBlinkMs = 500;
constexpr float kAutoplayCancelWidth = 120; // dp

} // namespace

bool App::watching() const {
    return m_mini.watch.has_value() || (!m_stack.empty() && m_stack.back().route == Route::Watch);
}

void App::onStop() {
    if (!m_settings.backgroundPlay) {
        m_player.setPaused(true);
    }
}

int App::idleWakeMs() const {
    const auto state = m_player.state();
    // An ended video keeps the loop awake only when autoplay is about to start the next one.
    const bool autoplay = state == media::PlayerState::Ended && m_watch.endedAt >= 0 &&
                          m_settings.autoplay && !m_watch.autoplayCancelled &&
                          nextVideo().has_value();
    const bool going =
        state == media::PlayerState::Playing || state == media::PlayerState::Loading || autoplay;
    if (going) {
        // Video frames wake the loop, but not the end of playback or a stall: a slow tick does.
        return m_onScreen && !m_pictureInPicture ? kPlayingWakeMs : kBackgroundWakeMs;
    }
    return m_typing ? kCaretBlinkMs : -1;
}

void App::onBridgeEvent(BridgeEvent event) {
    switch (event.kind) {
    case BridgeEvent::Kind::OpenVideo:
        openVideo(innertube::Item{.kind = innertube::ItemKind::Video, .id = event.videoId});
        break;
    case BridgeEvent::Kind::PictureInPicture:
        m_pictureInPicture = event.active;
        if (event.active) {
            m_watch.menu = PlayerMenu::None;
            m_watch.seek.reset();
            m_watch.fine = false;
        }
        break;
    case BridgeEvent::Kind::PictureInPictureDismissed:
        m_player.setPaused(true); // The window's close button.
        break;
    case BridgeEvent::Kind::Media:
        switch (event.command) {
        case MediaCommand::Play:
            if (m_player.state() == media::PlayerState::Ended) {
                m_player.seek(0);
            }
            m_player.setPaused(false);
            break;
        case MediaCommand::Pause:
            m_player.setPaused(true);
            break;
        case MediaCommand::Next:
            playNext();
            break;
        case MediaCommand::Previous:
            if (!m_watchHistory.empty()) {
                const bool minimized = m_mini.watch.has_value();
                playPrevious();
                if (minimized) {
                    goBack(); // Back into the mini player.
                }
            } else {
                m_player.seek(0);
            }
            break;
        case MediaCommand::Seek:
            m_player.seek(event.positionMs * 1000);
            break;
        case MediaCommand::Stop:
            m_player.setPaused(true);
            break;
        }
        break;
    }
}

void App::playNext() {
    if (!watching()) {
        return;
    }
    const auto next = nextVideo();
    if (!next) {
        return;
    }
    const bool minimized = m_mini.watch.has_value();
    openVideo(next->first, next->second);
    if (minimized) {
        goBack(); // The next video plays in the mini player too.
    }
}

void App::updateAutoplay() {
    if (m_player.state() != media::PlayerState::Ended || !watching()) {
        m_watch.endedAt = -1;
        return;
    }
    if (m_watch.endedAt < 0) {
        m_watch.endedAt = now();
    }
    if (!m_settings.autoplay || m_watch.autoplayCancelled || !nextVideo()) {
        return;
    }
    // On screen the countdown can be cancelled; unseen, nobody would.
    const bool seen = m_onScreen && !m_pictureInPicture;
    if (!seen || now() - m_watch.endedAt >= kAutoplayDelay) {
        playNext();
    }
}

void App::drawAutoplay(Frame& frame, ImVec2 min, ImVec2 max) {
    if (m_watch.endedAt < 0 || !m_settings.autoplay || m_watch.autoplayCancelled) {
        return;
    }
    const auto next = nextVideo();
    if (!next) {
        return;
    }
    const Canvas& c = frame.canvas;
    const float gutter = c.dp(theme::kGutter);
    c.rect(min, max, theme::kScrim);
    const auto remaining = static_cast<std::int64_t>(
        std::ceil(std::max(0.0, kAutoplayDelay - (frame.time - m_watch.endedAt))));
    const std::string heading = std::vformat(tr(Text::UpNextIn), std::make_format_args(remaining));
    const float width = max.x - min.x - gutter * 2;
    float y = min.y + (max.y - min.y) * 0.25f;
    c.line({min.x + gutter, y}, width, theme::kCaptionSize, theme::kTextSecondary, heading,
           TextAlign::Center);
    y += c.lineHeight(theme::kCaptionSize) + gutter * 0.5f;
    c.with(theme::kTitleWeight)
        .line({min.x + gutter, y}, width, theme::kTitleSize, theme::kTextPrimary, next->first.title,
              TextAlign::Center);
    y += c.lineHeight(theme::kTitleSize) + gutter;
    const float button = c.dp(kAutoplayCancelWidth);
    const ImVec2 cancelMin{(min.x + max.x) * 0.5f - button - gutter * 0.5f, y};
    const ImVec2 cancelMax{cancelMin.x + button, y + c.dp(theme::kButtonHeight)};
    if (pillButton(frame, tr(Text::Cancel), cancelMin, cancelMax, false)) {
        m_watch.autoplayCancelled = true;
    }
    const ImVec2 playMin{(min.x + max.x) * 0.5f + gutter * 0.5f, y};
    if (pillButton(frame, tr(Text::PlayNow), playMin,
                   {playMin.x + button, y + c.dp(theme::kButtonHeight)}, true)) {
        later([this] { playNext(); });
    }
    frame.animating = true; // The countdown.
}

void App::drawPictureInPicture(Frame& frame, ImVec2 display) {
    const Canvas& c = frame.canvas;
    c.rect({0, 0}, display, IM_COL32_BLACK);
    m_videoSurface->draw(c.list(), {0, 0}, display);
    const auto state = m_player.state();
    if (state == media::PlayerState::Loading || m_player.buffering()) {
        c.spinner({display.x * 0.5f, display.y * 0.5f}, theme::kSpinnerSize, theme::kTextPrimary,
                  frame.time);
        frame.animating = true;
    }
}

void App::syncPlayback() {
    const innertube::VideoDetails* details = m_player.details();
    const auto state = m_player.state();
    if (details == nullptr || state == media::PlayerState::Idle ||
        state == media::PlayerState::Failed) {
        // Between two videos (autoplay, next) the session stays: the service could not be
        // started again from the background.
        const bool open = !m_watch.item.id.empty() || !m_shorts.playing.empty();
        if (open) {
            if (m_published && (m_published->playing || !m_published->buffering)) {
                m_published->playing = false;
                m_published->buffering = state != media::PlayerState::Failed;
                m_platform.updatePlayback(*m_published);
                m_publishedAt = now();
            }
            return;
        }
        if (m_published) {
            m_platform.stopPlayback();
            m_published.reset();
        }
        if (m_pipAllowed) {
            m_platform.setPictureInPicture(false, 0, 0);
            m_pipAllowed = false;
        }
        return;
    }

    const bool onWatch = watching();
    const bool live = m_player.live();
    Platform::Playback playback{
        .videoId = details->id,
        .title = details->title,
        .channel = details->channel,
        .durationMs = live ? 0 : m_player.durationUs() / 1000,
        .positionMs = live ? 0 : m_player.positionUs() / 1000,
        .playing = state == media::PlayerState::Playing,
        .buffering = state == media::PlayerState::Loading || m_player.buffering(),
        .speed = m_player.speed(),
        .hasPrevious = onWatch && !m_watchHistory.empty(),
        .hasNext = onWatch && nextVideo().has_value(),
    };
    bool changed = !m_published;
    if (m_published) {
        const Platform::Playback& last = *m_published;
        changed = last.videoId != playback.videoId || last.title != playback.title ||
                  last.channel != playback.channel || last.durationMs != playback.durationMs ||
                  last.playing != playback.playing || last.buffering != playback.buffering ||
                  last.speed != playback.speed || last.hasPrevious != playback.hasPrevious ||
                  last.hasNext != playback.hasNext;
        if (!changed && playback.playing && !live) {
            // The session moves the position on by itself; only a seek needs telling.
            const auto expected =
                last.positionMs +
                static_cast<std::int64_t>((now() - m_publishedAt) * 1000 * last.speed);
            changed = std::abs(playback.positionMs - expected) > kPositionDriftMs;
        } else if (!changed && !playback.playing) {
            changed = std::abs(playback.positionMs - last.positionMs) > kPositionDriftMs;
        }
    }
    if (changed) {
        m_platform.updatePlayback(playback);
        m_published = std::move(playback);
        m_publishedAt = now();
    }

    // Leaving the app shrinks a playing video into a picture-in-picture window.
    const bool allowPip =
        m_settings.pictureInPicture && onWatch &&
        (state == media::PlayerState::Playing || state == media::PlayerState::Loading);
    const auto size = m_player.videoSize();
    if (allowPip != m_pipAllowed || (allowPip && size != m_pipSize)) {
        m_platform.setPictureInPicture(allowPip, size.first, size.second);
        m_pipAllowed = allowPip;
        m_pipSize = size;
    }
}

} // namespace sloptube::app
