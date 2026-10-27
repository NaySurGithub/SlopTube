#pragma once

#include <imgui.h>

#include <algorithm>
#include <cmath>

// Animation primitives: easing curves, frame-rate independent smoothing, and layers that move
// or fade everything drawn inside them.
namespace sloptube::app::motion {

// Durations (seconds) and curves follow Material motion, which the official app uses.
inline constexpr float kScreenDuration = 0.28f;
inline constexpr float kSheetDuration = 0.32f;
inline constexpr float kFadeDuration = 0.2f;
inline constexpr float kPopDuration = 0.35f;
inline constexpr float kPressScale = 0.96f;
inline constexpr float kShimmerPeriod = 1.4f;

[[nodiscard]] inline float easeOutCubic(float t) {
    const float inverse = 1 - std::clamp(t, 0.0f, 1.0f);
    return 1 - inverse * inverse * inverse;
}

[[nodiscard]] inline float easeInOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) * 0.5f;
}

// Overshooting curve for "pop" feedback (like buttons): rises past 1 then settles.
[[nodiscard]] inline float easeOutBack(float t) {
    constexpr float kOvershoot = 1.70158f;
    t = std::clamp(t, 0.0f, 1.0f) - 1;
    return 1 + t * t * ((kOvershoot + 1) * t + kOvershoot);
}

// Progress in [0, 1] of an animation that started at `start` and lasts `duration`.
[[nodiscard]] inline float progress(double now, double start, float duration) {
    return std::clamp(static_cast<float>((now - start) / duration), 0.0f, 1.0f);
}

// Moves `value` towards `target` exponentially; returns whether it is still moving.
inline bool approach(float& value, float target, float seconds, float rate = 14.0f) {
    value += (target - value) * (1 - std::exp(-rate * seconds));
    if (std::abs(target - value) < 0.002f) {
        value = target;
        return false;
    }
    return true;
}

[[nodiscard]] inline ImU32 withAlpha(ImU32 color, float alpha) {
    const auto base = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF);
    const auto scaled = static_cast<ImU32>(base * std::clamp(alpha, 0.0f, 1.0f));
    return (color & ~IM_COL32_A_MASK) | (scaled << IM_COL32_A_SHIFT);
}

// Captures what is drawn between begin() and end() so it can be translated, scaled about a
// pivot and faded as a whole, clip rectangles included.
class Layer {
public:
    explicit Layer(ImDrawList* list);

    void end(ImVec2 offset, float alpha = 1, float scale = 1, ImVec2 pivot = {});

private:
    ImDrawList* m_list;
    int m_firstCommand;
    int m_firstVertex;
};

} // namespace sloptube::app::motion
