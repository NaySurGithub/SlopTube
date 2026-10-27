#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace sloptube::app::media {

// Changes the tempo of interleaved 16-bit PCM without changing its pitch, by waveform-similarity
// overlap-add (WSOLA): output is assembled from short input segments, each picked near its
// nominal position where it best continues the previous one, and cross-faded into it. At speed 1
// segments are taken exactly where they belong, so the output equals the input.
//
// Runs on the real-time audio thread: nothing allocates after reset().
class TimeStretch {
public:
    // Reads up to `count` samples into `destination`; returns how many it read.
    using Read = std::function<std::size_t(std::int16_t* destination, std::size_t count)>;

    void reset(std::int32_t sampleRate, std::int32_t channels);

    // Writes `frames` frames to `out` at `speed` (1 is normal, 2 twice as fast), filling with
    // silence when `read` runs dry. Returns the input frames the written output stands for.
    double render(std::int16_t* out, std::size_t frames, double speed, const Read& read);

private:
    // Assembles the next hop into m_output; false when input is missing.
    bool produceHop(double speed, const Read& read);
    // Offset from `base` (an input frame) whose segment best continues m_tail.
    [[nodiscard]] std::ptrdiff_t bestOffset(std::size_t base) const;

    std::size_t m_channels = 0;
    std::size_t m_hop = 0;    // Output frames per hop; also the cross-fade length.
    std::size_t m_search = 0; // Frames searched either side of the nominal position.

    std::vector<std::int16_t> m_input; // Interleaved input not yet consumed.
    std::size_t m_inputFrames = 0;
    double m_nominal = 0; // Input frame where the next segment belongs.

    std::vector<std::int16_t> m_tail; // Natural continuation of the previous segment.
    bool m_hasTail = false;

    std::vector<std::int16_t> m_output; // One hop.
    std::size_t m_outputRead = 0;
    double m_outputRate = 1; // Input frames per output frame of the current hop.
};

} // namespace sloptube::app::media
