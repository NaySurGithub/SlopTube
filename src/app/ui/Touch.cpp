#include "app/ui/Touch.hpp"

#include <android/input.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace sloptube::app {
namespace {

// Velocity is estimated over this trailing window to ignore jitter of the final sample.
constexpr std::int64_t kVelocityWindowNs = 80'000'000;
constexpr float kNanosPerSecond = 1e9f;

bool inside(ImVec2 point, ImVec2 min, ImVec2 max) {
    return point.x >= min.x && point.x < max.x && point.y >= min.y && point.y < max.y;
}

} // namespace

void Touch::onMotionEvent(const AInputEvent* event) {
    const std::int32_t masked = AMotionEvent_getAction(event);
    const std::int32_t action = masked & AMOTION_EVENT_ACTION_MASK;
    const auto actionIndex =
        static_cast<std::size_t>((masked & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                                 AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    const std::int64_t time = AMotionEvent_getEventTime(event);
    const auto at = [&](std::size_t index) {
        return ImVec2{AMotionEvent_getX(event, index), AMotionEvent_getY(event, index)};
    };
    // Pointer indices change as pointers come and go; ids do not.
    const auto find = [&](std::int32_t id) -> std::optional<std::size_t> {
        for (std::size_t i = 0; i < AMotionEvent_getPointerCount(event); ++i) {
            if (AMotionEvent_getPointerId(event, i) == id) {
                return i;
            }
        }
        return std::nullopt;
    };
    const auto endPinch = [&] {
        if (m_secondary >= 0) {
            m_pendingPinchEnd = pinchScale().value_or(1);
            m_secondary = -1;
        }
    };

    switch (action) {
    case AMOTION_EVENT_ACTION_DOWN: {
        const ImVec2 position = at(0);
        m_primary = AMotionEvent_getPointerId(event, 0);
        m_secondary = -1;
        m_multi = false;
        m_down = true;
        m_dragging = false;
        m_start = position;
        m_downAt = std::chrono::steady_clock::now();
        m_position = position;
        m_lastFramePosition = position;
        m_sampleCount = 0;
        record(position, time);
        break;
    }
    case AMOTION_EVENT_ACTION_POINTER_DOWN:
        if (m_down && m_secondary < 0) {
            m_secondary = AMotionEvent_getPointerId(event, actionIndex);
            m_secondaryPosition = at(actionIndex);
            if (const auto primary = find(m_primary)) {
                m_position = at(*primary);
            }
            m_multi = true;
            m_dragging = false;
            m_pinchFrom = pinchDistance();
            m_pinchStart = {(m_position.x + m_secondaryPosition.x) * 0.5f,
                            (m_position.y + m_secondaryPosition.y) * 0.5f};
        }
        break;
    case AMOTION_EVENT_ACTION_MOVE:
        if (const auto primary = find(m_primary)) {
            const ImVec2 position = at(*primary);
            m_position = position;
            if (!m_multi) {
                record(position, time);
                if (!m_dragging &&
                    std::hypot(position.x - m_start.x, position.y - m_start.y) > m_slop) {
                    m_dragging = true;
                    m_lastFramePosition = position; // Avoid a jump by the slop distance.
                }
            }
        }
        if (m_secondary >= 0) {
            if (const auto secondary = find(m_secondary)) {
                m_secondaryPosition = at(*secondary);
            }
        }
        break;
    case AMOTION_EVENT_ACTION_POINTER_UP: {
        const std::int32_t id = AMotionEvent_getPointerId(event, actionIndex);
        if (id == m_primary && m_secondary >= 0) {
            const std::int32_t remaining = m_secondary;
            m_position = m_secondaryPosition;
            endPinch();
            m_primary = remaining;
        } else if (id == m_secondary) {
            endPinch();
        }
        break;
    }
    case AMOTION_EVENT_ACTION_UP: {
        const ImVec2 position = at(0);
        m_position = position;
        if (m_multi) {
            endPinch(); // Neither a tap nor a fling.
        } else {
            record(position, time);
            if (m_dragging) {
                m_pendingFling = velocity();
            } else {
                m_pendingTap = position;
            }
        }
        m_down = false;
        m_dragging = false;
        break;
    }
    case AMOTION_EVENT_ACTION_CANCEL:
        endPinch();
        m_down = false;
        m_dragging = false;
        break;
    default:
        break;
    }
}

void Touch::beginFrame() {
    m_blocked = false;
    m_tap = std::exchange(m_pendingTap, std::nullopt);
    m_fling = std::exchange(m_pendingFling, std::nullopt);
    m_pinchEnd = std::exchange(m_pendingPinchEnd, std::nullopt);
    if (m_dragging) {
        m_frameDelta = {m_position.x - m_lastFramePosition.x, m_position.y - m_lastFramePosition.y};
    } else {
        m_frameDelta = {};
    }
    m_lastFramePosition = m_position;
}

bool Touch::startedIn(ImVec2 min, ImVec2 max) const {
    return !m_blocked && inside(m_start, min, max);
}

bool Touch::tapped(ImVec2 min, ImVec2 max) {
    if (!m_blocked && m_tap && inside(*m_tap, min, max)) {
        m_tap.reset();
        return true;
    }
    return false;
}

bool Touch::pressed(ImVec2 min, ImVec2 max) const {
    return !m_blocked && m_down && !m_dragging && !m_multi && inside(m_start, min, max);
}

std::optional<float> Touch::pinchScale() const {
    if (m_blocked || m_secondary < 0 || m_pinchFrom <= 0) {
        return std::nullopt;
    }
    return pinchDistance() / m_pinchFrom;
}

float Touch::pinchDistance() const {
    return std::hypot(m_secondaryPosition.x - m_position.x, m_secondaryPosition.y - m_position.y);
}

float Touch::pressDuration() const {
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - m_downAt).count();
}

void Touch::record(ImVec2 position, std::int64_t timeNs) {
    if (m_sampleCount == m_samples.size()) {
        std::move(m_samples.begin() + 1, m_samples.end(), m_samples.begin());
        --m_sampleCount;
    }
    m_samples[m_sampleCount++] = {position, timeNs};
}

ImVec2 Touch::velocity() const {
    if (m_sampleCount < 2) {
        return {};
    }
    const Sample& last = m_samples[m_sampleCount - 1];
    std::size_t first = m_sampleCount - 2;
    while (first > 0 && last.timeNs - m_samples[first - 1].timeNs <= kVelocityWindowNs) {
        --first;
    }
    const Sample& origin = m_samples[first];
    const float seconds = static_cast<float>(last.timeNs - origin.timeNs) / kNanosPerSecond;
    if (seconds <= 0) {
        return {};
    }
    return {(last.position.x - origin.position.x) / seconds,
            (last.position.y - origin.position.y) / seconds};
}

} // namespace sloptube::app
