#pragma once

namespace sloptube::app {

// Seek undo, as in the official app: once a seek has taken the handle 25 dp away from where
// playback was, bringing it back within 20 dp cancels the seek if it is released in the next 2 s.
// Fed the handle's distance on every frame of the drag.
class SeekUndo {
public:
    // Returns true on the frame the undo arms, when the official app clicks.
    bool update(float distanceDp, double now) {
        m_moved = m_moved || distanceDp > kLeave;
        if (distanceDp >= kReach) {
            m_near = false;
            return false;
        }
        if (m_near) {
            return false;
        }
        m_near = true;
        m_nearSince = now;
        return m_moved;
    }

    [[nodiscard]] bool armed(double now) const {
        return m_moved && m_near && now - m_nearSince < kSeconds;
    }

private:
    static constexpr float kLeave = 25; // dp
    static constexpr float kReach = 20; // dp
    static constexpr double kSeconds = 2;

    bool m_moved = false;
    bool m_near = false;
    double m_nearSince = 0;
};

} // namespace sloptube::app
