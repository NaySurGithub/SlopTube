#include "app/ui/Scroller.hpp"

#include "app/ui/Theme.hpp"
#include "app/ui/Touch.hpp"

#include <algorithm>
#include <cmath>

namespace sloptube::app {

void Scroller::update(const Touch& touch, ImVec2 min, ImVec2 max, float contentExtent,
                      float seconds, float density) {
    const bool vertical = m_axis == Axis::Vertical;
    const auto along = [vertical](ImVec2 v) { return vertical ? v.y : v.x; };
    const auto across = [vertical](ImVec2 v) { return vertical ? v.x : v.y; };

    if (touch.dragging() && touch.startedIn(min, max)) {
        const ImVec2 travel{touch.position().x - touch.start().x,
                            touch.position().y - touch.start().y};
        // Claim the gesture only when it moves mostly along this axis, so a horizontal shelf
        // inside a vertical list does not steal vertical scrolls (and vice versa).
        if (m_tracking || std::abs(along(travel)) >= std::abs(across(travel))) {
            m_tracking = true;
            m_velocity = 0;
            m_offset -= along(touch.dragDelta());
        }
    } else if (m_tracking) {
        m_tracking = false;
        if (const auto fling = touch.flingVelocity()) {
            m_velocity = -along(*fling);
        }
    }

    if (!m_tracking && m_velocity != 0) {
        m_offset += m_velocity * seconds;
        m_velocity *= std::exp(-theme::kScrollFriction * seconds);
        if (std::abs(m_velocity) < theme::kScrollStopSpeed * density) {
            m_velocity = 0;
        }
    }

    const float limit = std::max(0.0f, contentExtent - along({max.x - min.x, max.y - min.y}));
    if (m_offset < 0 || m_offset > limit) {
        m_offset = std::clamp(m_offset, 0.0f, limit);
        m_velocity = 0;
    }
}

void Scroller::reset() {
    m_offset = 0;
    m_velocity = 0;
    m_tracking = false;
}

} // namespace sloptube::app
