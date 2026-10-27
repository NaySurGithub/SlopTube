#include "app/ui/VectorIcons.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace sloptube::app {
namespace {

// Shapes are described on a 24-unit grid (y down) as signed distances: negative inside.
struct Point {
    float x;
    float y;
};

constexpr float kGrid = 24;
constexpr float kStroke = 2; // Outline width of the official navigation icons.

float length(float x, float y) {
    return std::hypot(x, y);
}

float box(Point p, float x0, float y0, float x1, float y1, float radius) {
    const float qx = std::abs(p.x - (x0 + x1) * 0.5f) - ((x1 - x0) * 0.5f - radius);
    const float qy = std::abs(p.y - (y0 + y1) * 0.5f) - ((y1 - y0) * 0.5f - radius);
    return length(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) -
           radius;
}

float circle(Point p, Point center, float radius) {
    return length(p.x - center.x, p.y - center.y) - radius;
}

// Exact distance to a triangle (after Inigo Quilez).
float triangle(Point p, Point a, Point b, Point c) {
    const std::array<Point, 3> v{a, b, c};
    float distance = 1e9f;
    const float winding = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    bool inside = true;
    for (std::size_t i = 0; i < 3; ++i) {
        const Point& from = v[i];
        const Point& to = v[(i + 1) % 3];
        const float ex = to.x - from.x;
        const float ey = to.y - from.y;
        const float wx = p.x - from.x;
        const float wy = p.y - from.y;
        const float t = std::clamp((wx * ex + wy * ey) / (ex * ex + ey * ey), 0.0f, 1.0f);
        distance = std::min(distance, length(wx - ex * t, wy - ey * t));
        inside &= (ex * wy - ey * wx) * winding >= 0;
    }
    return inside ? -distance : distance;
}

float subtract(float shape, float hole) {
    return std::max(shape, -hole);
}

// The band of width `width` just inside the shape's edge.
float inner(float shape, float width) {
    return std::max(shape, -shape - width);
}

// Home: a roof over the walls with the door cut out.
float homeShape(Point p) {
    const float roof = triangle(p, {12, 3.6f}, {2.4f, 8.7f}, {21.6f, 8.7f}) - 1.4f;
    const float walls = box(p, 2.8f, 9, 21.2f, 21.6f, 1.5f);
    return std::min(roof, walls);
}

float homeDoor(Point p) {
    return box(p, 9.6f, 14.6f, 14.4f, 24, 0.6f);
}

// Shorts: an upright screen with a play triangle (a generic shape, not the official logo).
float shortsScreen(Point p) {
    return box(p, 5, 1.5f, 19, 22.5f, 3);
}

float shortsPlay(Point p) {
    return triangle(p, {10, 8.5f}, {10, 15.5f}, {15.5f, 12});
}

// Subscriptions: a screen with a play triangle under two bars, after the Material symbol.
float subscriptionsScreen(Point p) {
    return box(p, 2, 9, 22, 22, 1.6f);
}

float subscriptionsBar(Point p) {
    return std::min(box(p, 4, 1.5f, 20, 3, 0.6f), box(p, 3, 5, 21, 6.5f, 0.6f));
}

float subscriptionsPlay(Point p) {
    return triangle(p, {10, 12.5f}, {10, 18.5f}, {15.2f, 15.5f});
}

// You (signed out): head and shoulders.
float youHead(Point p) {
    return circle(p, {12, 7.5f}, 4.5f);
}

float youShoulders(Point p) {
    return std::max(circle(p, {12, 23}, 9), p.y - 21);
}

float glyph(Glyph which, Point p) {
    switch (which) {
    case Glyph::HomeOutline:
        return inner(subtract(homeShape(p), homeDoor(p)), kStroke);
    case Glyph::HomeFilled:
        return subtract(homeShape(p), homeDoor(p));
    case Glyph::ShortsOutline:
        return std::min(inner(shortsScreen(p), kStroke), shortsPlay(p));
    case Glyph::ShortsFilled:
        return subtract(shortsScreen(p), shortsPlay(p));
    case Glyph::SubscriptionsOutline:
        return std::min(
            {inner(subscriptionsScreen(p), kStroke), subscriptionsBar(p), subscriptionsPlay(p)});
    case Glyph::SubscriptionsFilled:
        return std::min(subtract(subscriptionsScreen(p), subscriptionsPlay(p)),
                        subscriptionsBar(p));
    case Glyph::YouOutline:
        return std::min(inner(youHead(p), kStroke), inner(youShoulders(p), kStroke));
    case Glyph::YouFilled:
        return std::min(youHead(p), youShoulders(p));
    case Glyph::Create: {
        constexpr float kArm = 0.8f;
        return std::min(box(p, 12 - kArm, 3.2f, 12 + kArm, 20.8f, kArm),
                        box(p, 3.2f, 12 - kArm, 20.8f, 12 + kArm, kArm));
    }
    case Glyph::Count:
        break;
    }
    return 1;
}

} // namespace

VectorIcons::VectorIcons(int sizePx)
    : m_size(sizePx) {
    constexpr auto kCount = static_cast<int>(Glyph::Count);
    const int width = m_size * kCount;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width * m_size));
    const float unitsPerPixel = kGrid / static_cast<float>(m_size);
    for (int index = 0; index < kCount; ++index) {
        for (int y = 0; y < m_size; ++y) {
            for (int x = 0; x < m_size; ++x) {
                const Point p{(static_cast<float>(x) + 0.5f) * unitsPerPixel,
                              (static_cast<float>(y) + 0.5f) * unitsPerPixel};
                // Coverage from the distance to the edge, one pixel wide.
                const float coverage = std::clamp(
                    0.5f - glyph(static_cast<Glyph>(index), p) / unitsPerPixel, 0.0f, 1.0f);
                const auto alpha = static_cast<std::uint32_t>(std::lround(coverage * 255));
                pixels[static_cast<std::size_t>(y * width + index * m_size + x)] =
                    0x00ffffffu | (alpha << 24);
            }
        }
    }
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, m_size, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels.data());
}

VectorIcons::~VectorIcons() {
    glDeleteTextures(1, &m_texture);
}

void VectorIcons::draw(ImDrawList* list, Glyph glyph, ImVec2 center, ImU32 color) const {
    constexpr auto kCount = static_cast<float>(Glyph::Count);
    const float index = static_cast<float>(std::to_underlying(glyph));
    const float half = static_cast<float>(m_size) * 0.5f;
    // Snapped to whole pixels so the texels map one to one.
    const ImVec2 min{std::round(center.x - half), std::round(center.y - half)};
    list->AddImage(ImTextureRef(static_cast<ImTextureID>(m_texture)), min,
                   {min.x + static_cast<float>(m_size), min.y + static_cast<float>(m_size)},
                   {index / kCount, 0}, {(index + 1) / kCount, 1}, color);
}

} // namespace sloptube::app
