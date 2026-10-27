#include "app/media/TimeStretch.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sloptube::app::media {
namespace {

constexpr double kHopSeconds = 0.010;    // Long enough for low voices, short enough not to echo.
constexpr double kSearchSeconds = 0.006; // About one pitch period of a low voice.
constexpr double kMaxSpeed = 4;          // Sizes the input buffer.
// Similarity is measured on every n-th frame, and candidate offsets on every m-th then refined:
// plenty of precision for speech and music at a fraction of the cost.
constexpr std::size_t kCorrelationStride = 4;
constexpr std::ptrdiff_t kCoarseStep = 2;

} // namespace

void TimeStretch::reset(std::int32_t sampleRate, std::int32_t channels) {
    m_channels = static_cast<std::size_t>(std::max(channels, 1));
    m_hop = static_cast<std::size_t>(sampleRate * kHopSeconds);
    m_search = static_cast<std::size_t>(sampleRate * kSearchSeconds);
    const auto capacity = static_cast<std::size_t>(static_cast<double>(m_search * 2 + m_hop * 2) +
                                                   static_cast<double>(m_hop) * kMaxSpeed);
    m_input.assign((capacity + m_hop) * m_channels, 0);
    m_inputFrames = 0;
    m_nominal = 0;
    m_tail.assign(m_hop * m_channels, 0);
    m_hasTail = false;
    m_output.assign(m_hop * m_channels, 0);
    m_outputRead = m_hop; // Empty.
    m_outputRate = 1;
}

double TimeStretch::render(std::int16_t* out, std::size_t frames, double speed, const Read& read) {
    double consumed = 0;
    std::size_t written = 0;
    while (written < frames) {
        if (m_outputRead == m_hop && !produceHop(speed, read)) {
            std::fill(out + written * m_channels, out + frames * m_channels, std::int16_t{0});
            break;
        }
        const std::size_t n = std::min(frames - written, m_hop - m_outputRead);
        std::copy_n(m_output.data() + m_outputRead * m_channels, n * m_channels,
                    out + written * m_channels);
        m_outputRead += n;
        written += n;
        consumed += static_cast<double>(n) * m_outputRate;
    }
    return consumed;
}

bool TimeStretch::produceHop(double speed, const Read& read) {
    const std::size_t capacity = m_input.size() / m_channels;
    m_inputFrames +=
        read(m_input.data() + m_inputFrames * m_channels, (capacity - m_inputFrames) * m_channels) /
        m_channels;

    // At normal speed the segment is the one that follows; otherwise the best match nearby.
    const bool search = m_hasTail && speed != 1;
    const auto base = static_cast<std::size_t>(std::lround(m_nominal));
    if (base + m_hop * 2 + (search ? m_search : 0) > m_inputFrames) {
        return false;
    }
    const std::size_t start =
        search ? static_cast<std::size_t>(static_cast<std::ptrdiff_t>(base) + bestOffset(base))
               : base;

    const std::int16_t* segment = m_input.data() + start * m_channels;
    const std::size_t samples = m_hop * m_channels;
    if (m_hasTail) {
        for (std::size_t frame = 0; frame < m_hop; ++frame) {
            const float in = (static_cast<float>(frame) + 0.5f) / static_cast<float>(m_hop);
            for (std::size_t channel = 0; channel < m_channels; ++channel) {
                const std::size_t i = frame * m_channels + channel;
                m_output[i] =
                    static_cast<std::int16_t>(std::lround(static_cast<float>(m_tail[i]) * (1 - in) +
                                                          static_cast<float>(segment[i]) * in));
            }
        }
    } else {
        std::copy_n(segment, samples, m_output.data());
    }
    std::copy_n(segment + samples, samples, m_tail.data());
    m_hasTail = true;
    m_outputRead = 0;
    m_outputRate = speed;
    m_nominal += static_cast<double>(m_hop) * speed;

    // Keep what the next search may still look back at.
    const auto next = static_cast<std::size_t>(std::lround(m_nominal));
    const std::size_t drop = std::min(next > m_search ? next - m_search : 0, m_inputFrames);
    if (drop > 0) {
        std::memmove(m_input.data(), m_input.data() + drop * m_channels,
                     (m_inputFrames - drop) * m_channels * sizeof(std::int16_t));
        m_inputFrames -= drop;
        m_nominal -= static_cast<double>(drop);
    }
    return true;
}

std::ptrdiff_t TimeStretch::bestOffset(std::size_t base) const {
    // Normalised cross-correlation of the channel sums, which ignores loudness differences.
    const auto score = [&](std::ptrdiff_t offset) {
        const std::int16_t* candidate =
            m_input.data() +
            static_cast<std::size_t>(static_cast<std::ptrdiff_t>(base) + offset) * m_channels;
        double dot = 0;
        double energy = 1;
        for (std::size_t frame = 0; frame < m_hop; frame += kCorrelationStride) {
            double tail = 0;
            double sample = 0;
            for (std::size_t channel = 0; channel < m_channels; ++channel) {
                tail += m_tail[frame * m_channels + channel];
                sample += candidate[frame * m_channels + channel];
            }
            dot += tail * sample;
            energy += sample * sample;
        }
        return dot / std::sqrt(energy);
    };

    const auto lowest = -static_cast<std::ptrdiff_t>(std::min(base, m_search));
    const auto highest = static_cast<std::ptrdiff_t>(m_search);
    std::ptrdiff_t best = 0;
    double bestScore = score(0);
    for (std::ptrdiff_t offset = lowest; offset <= highest; offset += kCoarseStep) {
        if (const double s = score(offset); s > bestScore) {
            best = offset;
            bestScore = s;
        }
    }
    for (const std::ptrdiff_t offset : {best - 1, best + 1}) {
        if (offset >= lowest && offset <= highest) {
            if (const double s = score(offset); s > bestScore) {
                best = offset;
                bestScore = s;
            }
        }
    }
    return best;
}

} // namespace sloptube::app::media
