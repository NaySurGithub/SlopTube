#pragma once

#include <imgui.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

struct AInputEvent;

namespace sloptube::app {

// Gesture recogniser producing taps, drags and fling velocities from the first pointer, and pinches
// from the first two. Once a second pointer lands the gesture is a pinch until every pointer is
// up: the tap, drag or press in progress is cancelled and none starts. Events arrive between
// frames; queries describe the frame being built.
class Touch {
public:
    explicit Touch(float slopPx)
        : m_slop(slopPx) {}

    // Movement (px) before a press becomes a drag; follows the interface size.
    void setSlop(float slopPx) { m_slop = slopPx; }

    void onMotionEvent(const AInputEvent* event);
    void beginFrame();

    [[nodiscard]] bool down() const { return m_down; }

    [[nodiscard]] bool dragging() const { return m_dragging && !m_blocked; }

    [[nodiscard]] ImVec2 position() const { return m_position; }

    [[nodiscard]] ImVec2 start() const { return m_start; }

    // Movement since the previous frame while dragging.
    [[nodiscard]] ImVec2 dragDelta() const { return m_frameDelta; }

    // Set on the frame a drag ends, in px/s.
    [[nodiscard]] std::optional<ImVec2> flingVelocity() const { return m_fling; }

    // Whether the gesture began inside the rectangle (owners of a drag test this).
    [[nodiscard]] bool startedIn(ImVec2 min, ImVec2 max) const;

    // A tap landing inside the rectangle this frame; consumed by the first caller.
    [[nodiscard]] bool tapped(ImVec2 min, ImVec2 max);

    // Pressed state inside the rectangle, for press highlights.
    [[nodiscard]] bool pressed(ImVec2 min, ImVec2 max) const;

    // While blocked, the gesture belongs to an overlay: queries report no taps, presses or drags
    // for whatever is drawn underneath.
    void setBlocked(bool blocked) { m_blocked = blocked; }

    [[nodiscard]] bool blocked() const { return m_blocked; }

    // Seconds since the current press began, for press ripples.
    [[nodiscard]] float pressDuration() const;

    // While two pointers are down: their distance over the distance when the second one landed.
    [[nodiscard]] std::optional<float> pinchScale() const;

    // Final scale of a pinch, set on the frame its second pointer lifts.
    [[nodiscard]] std::optional<float> pinchEnded() const { return m_pinchEnd; }

    // Point the pinch started around (the midpoint of both pointers).
    [[nodiscard]] ImVec2 pinchStart() const { return m_pinchStart; }

private:
    struct Sample {
        ImVec2 position;
        std::int64_t timeNs = 0;
    };

    void record(ImVec2 position, std::int64_t timeNs);
    [[nodiscard]] float pinchDistance() const;
    [[nodiscard]] ImVec2 velocity() const;

    float m_slop;
    bool m_blocked = false;
    bool m_down = false;
    bool m_dragging = false;
    ImVec2 m_start{};
    std::chrono::steady_clock::time_point m_downAt;
    ImVec2 m_position{};
    ImVec2 m_lastFramePosition{};
    ImVec2 m_frameDelta{};

    std::optional<ImVec2> m_pendingTap;
    std::optional<ImVec2> m_tap;
    std::optional<ImVec2> m_pendingFling;
    std::optional<ImVec2> m_fling;

    // Pointer ids: the first pointer, and the second while one is down (-1 otherwise).
    std::int32_t m_primary = -1;
    std::int32_t m_secondary = -1;
    ImVec2 m_secondaryPosition{};
    bool m_multi = false; // A second pointer landed during this gesture.
    float m_pinchFrom = 0;
    ImVec2 m_pinchStart{};
    std::optional<float> m_pendingPinchEnd;
    std::optional<float> m_pinchEnd;

    std::array<Sample, 8> m_samples{};
    std::size_t m_sampleCount = 0;
};

} // namespace sloptube::app
