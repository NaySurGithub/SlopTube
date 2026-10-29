#pragma once

#include <imgui.h>

namespace sloptube::app {

class Touch;

enum class Axis : unsigned char {
    Vertical,
    Horizontal,
};

// Kinetic scrolling for one viewport: follows drags that start inside it, then decays flings.
class Scroller {
public:
    Scroller() = default;

    explicit Scroller(Axis axis)
        : m_axis(axis) {}

    void update(const Touch& touch, ImVec2 min, ImVec2 max, float contentExtent, float seconds,
                float density);

    [[nodiscard]] float offset() const { return m_offset; }

    [[nodiscard]] bool moving() const { return m_velocity != 0 || m_tracking; }

    void reset();

    // Jumps to `offset`, kept within the content by the next update().
    void setOffset(float offset) {
        m_offset = offset;
        m_velocity = 0;
    }

private:
    Axis m_axis = Axis::Vertical;
    float m_offset = 0;
    float m_velocity = 0; // px/s along the axis, positive moves content forward.
    bool m_tracking = false;
};

} // namespace sloptube::app
