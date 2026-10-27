#include "app/ui/Logo.hpp"

#include "app/ui/LogoShape.hpp"
#include "app/ui/Motion.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace sloptube::app {
namespace {

constexpr int kCurveSegments = 8;

using Polygon = std::vector<ImVec2>;

// Flattens the subset of SVG path data the logo uses: absolute M, L, C and Z.
std::vector<Polygon> parsePath(std::string_view data) {
    std::vector<Polygon> polygons;
    std::size_t i = 0;
    const auto number = [&] {
        while (i < data.size() && (data[i] == ' ' || data[i] == ',')) {
            ++i;
        }
        // The path is a string literal, so strtof stops at its terminator at the latest.
        char* end = nullptr;
        const float value = std::strtof(data.data() + i, &end);
        i = static_cast<std::size_t>(end - data.data());
        return value;
    };
    const auto point = [&] {
        const float x = number();
        return ImVec2{x, number()};
    };
    while (i < data.size()) {
        const char command = data[i++];
        switch (command) {
        case 'M':
            polygons.push_back({point()});
            break;
        case 'L':
            polygons.back().push_back(point());
            break;
        case 'C': {
            const ImVec2 p0 = polygons.back().back();
            const ImVec2 p1 = point();
            const ImVec2 p2 = point();
            const ImVec2 p3 = point();
            for (int s = 1; s <= kCurveSegments; ++s) {
                polygons.back().push_back(
                    ImBezierCubicCalc(p0, p1, p2, p3, static_cast<float>(s) / kCurveSegments));
            }
            break;
        }
        default: // 'Z' and separators.
            break;
        }
    }
    return polygons;
}

struct Shape {
    std::vector<Polygon> play = parsePath(logo::kPlay);
    std::vector<Polygon> drips = parsePath(logo::kDrips);
};

const Shape& shape() {
    static const Shape parsed;
    return parsed;
}

ImU32 mix(ImU32 from, ImU32 to, float t) {
    const ImVec4 a = ImGui::ColorConvertU32ToFloat4(from);
    const ImVec4 b = ImGui::ColorConvertU32ToFloat4(to);
    return ImGui::ColorConvertFloat4ToU32(ImLerp(a, b, t));
}

} // namespace

void drawLogo(ImDrawList* list, ImVec2 at, ImVec2 anchor, float scale, LogoStyle style) {
    const auto map = [&](ImVec2 p) {
        return ImVec2{at.x + (p.x - anchor.x) * scale, at.y + (p.y - anchor.y) * scale};
    };
    // Drawn white with the final alpha, then shaded with the gradient (alpha kept).
    const int first = list->VtxBuffer.Size;
    const ImU32 color = motion::withAlpha(IM_COL32_WHITE, style.alpha);
    const float stroke = logo::kStroke * scale;
    Polygon points;
    for (const Polygon& polygon : shape().play) {
        points.clear();
        std::ranges::transform(polygon, std::back_inserter(points), map);
        list->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), color);
        list->AddPolyline(points.data(), static_cast<int>(points.size()), color, stroke,
                          ImDrawFlags_Closed);
        for (const ImVec2& corner : points) {
            list->AddCircleFilled(corner, stroke * 0.5f, color);
        }
    }
    const ImU32 drips = motion::withAlpha(IM_COL32_WHITE, style.alpha * style.dripsAlpha);
    if (style.dripsAlpha > 0) {
        for (const Polygon& polygon : shape().drips) {
            points.clear();
            std::ranges::transform(polygon, std::back_inserter(points), map);
            list->AddConcavePolyFilled(points.data(), static_cast<int>(points.size()), drips);
        }
    }
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(
        list, first, list->VtxBuffer.Size, map(logo::kGradientStart), map(logo::kGradientEnd),
        mix(logo::kStartColor, IM_COL32_WHITE, style.white),
        mix(logo::kEndColor, IM_COL32_WHITE, style.white));
}

const LogoBounds& logoPlayBounds() {
    static const LogoBounds bounds = [] {
        LogoBounds b{{logo::kViewport, logo::kViewport}, {0, 0}};
        for (const Polygon& polygon : shape().play) {
            for (const ImVec2& p : polygon) {
                b.min = ImMin(b.min, p);
                b.max = ImMax(b.max, p);
            }
        }
        const float half = logo::kStroke * 0.5f;
        b.min = {b.min.x - half, b.min.y - half};
        b.max = {b.max.x + half, b.max.y + half};
        return b;
    }();
    return bounds;
}

} // namespace sloptube::app
