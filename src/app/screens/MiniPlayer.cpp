// Mini player: dragging the watch page's video down (or pressing Back) shrinks it into a floating
// window that keeps playing over the rest of the app. The window can be dragged and snaps to the
// nearest corner; a tap shows its controls (close, play/pause, reopen).

#include "app/App.hpp"
#include "app/ui/Motion.hpp"
#include "app/ui/Theme.hpp"

namespace sloptube::app {
namespace {

constexpr float kWidthFraction = 0.5f;     // Of the screen width.
constexpr float kMargin = 12;              // dp between the window and the screen's edges and bars.
constexpr float kMinimizeFraction = 0.18f; // Of the screen height a pull must reach to minimise.
constexpr float kMinimizeFling = 800;      // dp/s of a downward fling that minimises regardless.
constexpr float kSnapRate = 14;            // Exponential rate of the corner snap and pull release.
constexpr float kFlingLead = 0.15f;        // s of fling velocity added when picking a corner.
constexpr float kProgressHeight = 2;       // dp
constexpr float kShadow = 6;               // dp
constexpr ImU32 kShadowColor = IM_COL32(0, 0, 0, 0x80);
constexpr ImU32 kControlsScrim = IM_COL32(0, 0, 0, 0x80);

ImVec2 lerp(ImVec2 a, ImVec2 b, float t) {
    return {std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t)};
}

} // namespace

float App::pullWatch(Frame& frame, ImVec2 videoMin, ImVec2 videoMax) {
    // Not while a drag seeks: easy seek starts from a hold, precise seeking takes every drag.
    const bool allowed = !m_fullscreen && m_watch.menu == PlayerMenu::None &&
                         m_sheet.kind == Sheet::None && !m_speedHeld && !m_watch.seek &&
                         !m_watch.fine;
    const ImVec2 travel{frame.touch.position().x - frame.touch.start().x,
                        frame.touch.position().y - frame.touch.start().y};
    if (allowed && frame.touch.dragging() && frame.touch.startedIn(videoMin, videoMax) &&
        (m_mini.pull > 0 || (travel.y > 0 && travel.y > std::abs(travel.x)))) {
        m_mini.pull = std::max(0.0f, travel.y);
        frame.animating = true;
        return m_mini.pull;
    }
    if (m_mini.pull <= 0) {
        return 0;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const auto fling = frame.touch.flingVelocity();
    if (m_mini.pull > display.y * kMinimizeFraction ||
        (fling && fling->y > frame.dp(kMinimizeFling))) {
        const float pull = m_mini.pull;
        m_mini.pull = 0;
        later([this, videoMin, videoMax, pull] {
            minimizeVideo({videoMin.x, videoMin.y + pull}, {videoMax.x, videoMax.y + pull});
        });
        return pull;
    }
    // Released too early: spring back.
    if (motion::approach(m_mini.pull, 0, frame.seconds, kSnapRate)) {
        frame.animating = true;
    }
    return m_mini.pull;
}

void App::minimizeVideo(ImVec2 videoMin, ImVec2 videoMax) {
    if (m_stack.empty() || m_stack.back().route != Route::Watch) {
        return;
    }
    m_sheet = SheetState{};
    m_mini.watch = std::move(m_stack.back());
    m_stack.pop_back();
    m_mini.pull = 0;
    m_mini.minimizedAt = now();
    m_mini.fromMin = videoMin;
    m_mini.fromMax = videoMax;
    m_mini.controls = false;
    m_mini.dragging = false;
}

void App::restoreVideo() {
    if (!m_mini.watch) {
        return;
    }
    Screen watch = std::move(*m_mini.watch);
    m_mini.watch.reset();
    push(std::move(watch));
}

void App::closeMiniPlayer() {
    if (!m_mini.watch) {
        return;
    }
    closeVideo();
    m_watchHistory.clear();
}

void App::drawMiniPlayer(Frame& frame, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    ImDrawList* list = c.list();
    const float margin = c.dp(kMargin);
    const ImVec2 size{(max.x - min.x) * kWidthFraction,
                      (max.x - min.x) * kWidthFraction * theme::kAspect16x9};
    // Corners clear the top bar and the navigation bar, like the official floating player.
    const float left = min.x + margin;
    const float right = max.x - margin - size.x;
    const float top = min.y + c.dp(theme::kTopBarHeight) + margin;
    const float bottom = max.y - c.dp(theme::kBottomBarHeight) - margin - size.y;
    const std::array<ImVec2, 4> corners{ImVec2{left, top}, ImVec2{right, top}, ImVec2{left, bottom},
                                        ImVec2{right, bottom}};
    if (m_mini.position.x < 0) {
        m_mini.position = corners[m_mini.corner];
    }

    // Dragging moves the window with the finger; releasing snaps it to the nearest corner,
    // thrown in the direction of a fling.
    if (frame.touch.dragging() && !m_mini.dragging &&
        frame.touch.startedIn(m_mini.min, m_mini.max)) {
        m_mini.dragging = true;
        m_mini.grabbed = m_mini.position;
    }
    if (m_mini.dragging) {
        if (frame.touch.down()) {
            m_mini.position = {m_mini.grabbed.x + frame.touch.position().x - frame.touch.start().x,
                               m_mini.grabbed.y + frame.touch.position().y - frame.touch.start().y};
        } else {
            m_mini.dragging = false;
            ImVec2 aim = m_mini.position;
            if (const auto fling = frame.touch.flingVelocity()) {
                aim.x += fling->x * kFlingLead;
                aim.y += fling->y * kFlingLead;
            }
            float nearest = std::numeric_limits<float>::max();
            for (std::size_t i = 0; i < corners.size(); ++i) {
                const float distance = std::hypot(corners[i].x - aim.x, corners[i].y - aim.y);
                if (distance < nearest) {
                    nearest = distance;
                    m_mini.corner = i;
                }
            }
        }
        frame.animating = true;
    } else {
        const ImVec2 target = corners[m_mini.corner];
        const bool movingX =
            motion::approach(m_mini.position.x, target.x, frame.seconds, kSnapRate);
        const bool movingY =
            motion::approach(m_mini.position.y, target.y, frame.seconds, kSnapRate);
        frame.animating |= movingX || movingY;
    }

    // Shrinking from where the video was on the watch page.
    const float shrink = motion::easeOutCubic(
        motion::progress(frame.time, m_mini.minimizedAt, motion::kSheetDuration));
    if (shrink < 1) {
        frame.animating = true;
    }
    const ImVec2 windowMin = lerp(m_mini.fromMin, m_mini.position, shrink);
    const ImVec2 windowMax =
        lerp(m_mini.fromMax, {m_mini.position.x + size.x, m_mini.position.y + size.y}, shrink);
    m_mini.min = windowMin;
    m_mini.max = windowMax;

    const float shadow = c.dp(kShadow);
    c.rect({windowMin.x - shadow * 0.5f, windowMin.y},
           {windowMax.x + shadow * 0.5f, windowMax.y + shadow}, kShadowColor, kShadow);
    c.rect(windowMin, windowMax, IM_COL32_BLACK, theme::kCornerRadius);
    m_videoSurface->draw(list, windowMin, windowMax);

    const auto state = m_player.state();
    const float duration = static_cast<float>(m_player.durationUs());
    if (duration > 0) {
        const float fraction =
            std::clamp(static_cast<float>(m_player.positionUs()) / duration, 0.0f, 1.0f);
        const float barTop = windowMax.y - c.dp(kProgressHeight);
        c.rect({windowMin.x, barTop}, windowMax, theme::kTrack);
        c.rect({windowMin.x, barTop}, {std::lerp(windowMin.x, windowMax.x, fraction), windowMax.y},
               theme::kAccent);
    }
    // The bar moves with the video frames, which wake the loop themselves.
    if (state == media::PlayerState::Loading || m_player.buffering()) {
        c.spinner({(windowMin.x + windowMax.x) * 0.5f, (windowMin.y + windowMax.y) * 0.5f},
                  theme::kSpinnerSize, theme::kTextPrimary, frame.time);
        frame.animating = true;
    }

    if (m_mini.controls && frame.time - m_mini.controlsShownAt > theme::kControlsTimeout) {
        m_mini.controls = false;
    }
    // A second tap soon after the first reopens the watch page, before the controls the first
    // one showed can take it.
    if (!m_mini.dragging && frame.time - m_mini.lastTapAt < theme::kDoubleTap &&
        frame.touch.tapped(windowMin, windowMax)) {
        m_mini.lastTapAt = -1;
        later([this] { restoreVideo(); });
        return;
    }
    if (m_mini.controls) {
        c.rect(windowMin, windowMax, kControlsScrim);
        const float inset = c.dp(theme::kIconSize);
        const ImVec2 center{(windowMin.x + windowMax.x) * 0.5f, (windowMin.y + windowMax.y) * 0.5f};
        const bool playing = state == media::PlayerState::Playing;
        if (iconButton(frame, Icon::Close, {windowMin.x + inset, windowMin.y + inset},
                       theme::kTextPrimary)) {
            later([this] { closeMiniPlayer(); });
            return;
        }
        if (iconButton(frame, Icon::Fullscreen, {windowMax.x - inset, windowMin.y + inset},
                       theme::kTextPrimary)) {
            later([this] { restoreVideo(); });
            return;
        }
        if (iconButton(frame, playing ? Icon::Pause : Icon::Play, center, theme::kTextPrimary)) {
            m_player.setPaused(playing);
            m_mini.controlsShownAt = frame.time;
            return;
        }
    }
    // A single tap toggles the controls.
    if (m_mini.dragging || !frame.touch.tapped(windowMin, windowMax)) {
        return;
    }
    m_mini.lastTapAt = frame.time;
    m_mini.controls = !m_mini.controls;
    m_mini.controlsShownAt = frame.time;
}

} // namespace sloptube::app
