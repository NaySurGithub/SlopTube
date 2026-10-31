#include "app/ui/Canvas.hpp"

#include "app/ui/Theme.hpp"
#include "app/ui/text/TextLayout.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <numbers>

namespace sloptube::app {
namespace {

constexpr int kSpinnerSegments = 24;
constexpr float kSpinnerSweep = 1.5f * std::numbers::pi_v<float>;
constexpr float kSpinnerSpeed = 6.0f; // Radians per second.

// Distinct, readable avatar fills chosen by hashing the channel name.
constexpr std::array<ImU32, 6> kAvatarColors{
    IM_COL32(0xe9, 0x1e, 0x63, 0xff), IM_COL32(0x67, 0x3a, 0xb7, 0xff),
    IM_COL32(0x03, 0xa9, 0xf4, 0xff), IM_COL32(0x00, 0x96, 0x88, 0xff),
    IM_COL32(0xff, 0x98, 0x00, 0xff), IM_COL32(0x79, 0x55, 0x48, 0xff),
};

// Byte length of the UTF-8 sequence starting with `lead`.
std::size_t utf8Length(unsigned char lead) {
    if (lead < 0x80) {
        return 1;
    }
    if ((lead >> 5) == 0x6) {
        return 2;
    }
    if ((lead >> 4) == 0xE) {
        return 3;
    }
    return 4;
}

} // namespace

Canvas Canvas::with(Weight weight) const {
    Canvas copy = *this;
    copy.m_font = m_fonts.get(weight);
    return copy;
}

void Canvas::draw(ImVec2 position, float size, ImU32 color, std::string_view text) const {
    if (text.empty()) {
        return; // ImGui would strlen() a null data pointer.
    }
    m_list->AddText(m_font, size, position, color, text.data(), text.data() + text.size());
}

float Canvas::lineHeight(float sizeSp) const {
    return dp(sizeSp) * m_fonts.lineSpacing;
}

ImVec2 Canvas::measure(float sizeSp, std::string_view text) const {
    const float size = dp(sizeSp);
    if (text.empty()) {
        return {}; // Also guards ImGui from strlen() on a null data pointer.
    }
    ImVec2 extent;
    if (const text::Paragraph* laid = text::layoutBidi(m_font, size, text, FLT_MAX, INT_MAX)) {
        for (const text::VisualLine& visual : laid->lines) {
            extent.x = std::max(extent.x, visual.width);
        }
        extent.y = lineHeight(sizeSp) * static_cast<float>(laid->lines.size());
    } else {
        // ImGui advances lines by the font size; ours are Android's line height.
        extent = m_font->CalcTextSizeA(size, FLT_MAX, 0, text.data(), text.data() + text.size());
        extent.y *= m_fonts.lineSpacing;
    }
    return extent;
}

void Canvas::text(ImVec2 position, float sizeSp, ImU32 color, std::string_view text) const {
    const float size = dp(sizeSp);
    if (const text::Paragraph* laid = text::layoutBidi(m_font, size, text, FLT_MAX, INT_MAX)) {
        for (const text::VisualLine& visual : laid->lines) {
            draw(position, size, color, visual.text);
            position.y += lineHeight(sizeSp);
        }
        return;
    }
    draw(position, size, color, text);
}

void Canvas::line(ImVec2 position, float width, float sizeSp, ImU32 color, std::string_view text,
                  TextAlign align) const {
    paragraph(position, width, sizeSp, color, text, 1, align);
}

float Canvas::paragraph(ImVec2 position, float width, float sizeSp, ImU32 color,
                        std::string_view text, int maxLines, TextAlign align) const {
    const float size = dp(sizeSp);
    const float advance = lineHeight(sizeSp);
    int lines = 0;
    const auto origin = [&](float lineWidth, bool rtl) {
        const float slack = width - lineWidth;
        const float x = align == TextAlign::Center ? slack * 0.5f : rtl ? slack : 0;
        return ImVec2{position.x + x, position.y + static_cast<float>(lines++) * advance};
    };

    if (const text::Paragraph* laid = text::layoutBidi(m_font, size, text, width, maxLines)) {
        for (const text::VisualLine& visual : laid->lines) {
            draw(origin(visual.width, laid->rtl), size, color, visual.text);
        }
        return static_cast<float>(lines) * advance;
    }

    // Left-to-right only: draw straight from the source, measuring only what alignment needs.
    text::wrapLines(m_font, size, text, width, maxLines, [&](std::string_view line, bool cut) {
        const float used =
            cut || align != TextAlign::Natural
                ? m_font->CalcTextSizeA(size, FLT_MAX, 0, line.data(), line.data() + line.size()).x
                : 0;
        const float ellipsis = cut ? measure(sizeSp, text::kEllipsis).x : 0;
        const ImVec2 at = origin(used + ellipsis, false);
        draw(at, size, color, line);
        if (cut) {
            draw({at.x + used, at.y}, size, color, text::kEllipsis);
        }
    });
    return static_cast<float>(lines) * advance;
}

int Canvas::lineCount(float width, float sizeSp, std::string_view text, int maxLines) const {
    const float size = dp(sizeSp);
    if (const text::Paragraph* laid = text::layoutBidi(m_font, size, text, width, maxLines)) {
        return static_cast<int>(laid->lines.size());
    }
    int lines = 0;
    text::wrapLines(m_font, size, text, width, maxLines, [&](std::string_view, bool) { ++lines; });
    return lines;
}

void Canvas::rect(ImVec2 min, ImVec2 max, ImU32 color, float roundingDp) const {
    m_list->AddRectFilled(min, max, color, dp(roundingDp));
}

void Canvas::avatar(ImVec2 center, float sizeDp, std::string_view name) const {
    std::size_t hash = 0;
    for (const char c : name) {
        hash = hash * 31 + static_cast<unsigned char>(c);
    }
    const float radius = dp(sizeDp) * 0.5f;
    m_list->AddCircleFilled(center, radius, kAvatarColors[hash % kAvatarColors.size()]);
    if (name.empty()) {
        return;
    }
    const auto initial = name.substr(0, utf8Length(static_cast<unsigned char>(name[0])));
    const float sizeSp = sizeDp * 0.45f;
    const ImVec2 extent = measure(sizeSp, initial);
    text({center.x - extent.x * 0.5f, center.y - extent.y * 0.5f}, sizeSp, theme::kTextPrimary,
         initial);
}

void Canvas::spinner(ImVec2 center, float sizeDp, ImU32 color, double time) const {
    const float radius = dp(sizeDp) * 0.5f;
    const float start = static_cast<float>(std::fmod(time * kSpinnerSpeed, 2 * std::numbers::pi));
    m_list->PathArcTo(center, radius, start, start + kSpinnerSweep, kSpinnerSegments);
    m_list->PathStroke(color, dp(theme::kIconStroke) * 1.5f);
}

void Canvas::icon(Icon icon, ImVec2 c, float sizeDp, ImU32 color) const {
    // Glyphs are designed on a 24-unit grid centred on `c`.
    const float unit = dp(sizeDp) / 24.0f;
    const float stroke = dp(theme::kIconStroke);
    const auto p = [&](float x, float y) { return ImVec2{c.x + x * unit, c.y + y * unit}; };

    switch (icon) {
    case Icon::Home: {
        const std::array points{p(-8, -1), p(0, -8), p(8, -1), p(8, 8), p(-8, 8)};
        m_list->AddPolyline(points.data(), static_cast<int>(points.size()), color, stroke,
                            ImDrawFlags_Closed);
        m_list->AddRect(p(-2.5f, 2), p(2.5f, 8), color, 0, stroke);
        break;
    }
    case Icon::Subscriptions:
        m_list->AddRect(p(-9, -4), p(9, 9), color, unit * 2, stroke);
        m_list->AddLine(p(-7, -7), p(7, -7), color, stroke);
        m_list->AddLine(p(-5, -10), p(5, -10), color, stroke);
        m_list->AddTriangleFilled(p(-2.5f, -0.5f), p(-2.5f, 5.5f), p(3.5f, 2.5f), color);
        break;
    case Icon::Library:
        m_list->AddCircle(p(0, -3), unit * 4, color, 0, stroke);
        m_list->PathArcTo(p(0, 10), unit * 8, std::numbers::pi_v<float> * 1.15f,
                          std::numbers::pi_v<float> * 1.85f);
        m_list->PathStroke(color, stroke);
        break;
    case Icon::Search:
        m_list->AddCircle(p(-2, -2), unit * 6.5f, color, 0, stroke);
        m_list->AddLine(p(2.8f, 2.8f), p(9, 9), color, stroke);
        break;
    case Icon::Back:
        m_list->AddLine(p(-8, 0), p(8, 0), color, stroke);
        m_list->AddLine(p(-8, 0), p(-1, -7), color, stroke);
        m_list->AddLine(p(-8, 0), p(-1, 7), color, stroke);
        break;
    case Icon::Close:
        m_list->AddLine(p(-7, -7), p(7, 7), color, stroke);
        m_list->AddLine(p(-7, 7), p(7, -7), color, stroke);
        break;
    case Icon::Play:
        m_list->AddTriangleFilled(p(-6, -9), p(-6, 9), p(9, 0), color);
        break;
    case Icon::Pause:
        m_list->AddRectFilled(p(-7, -8), p(-2, 8), color);
        m_list->AddRectFilled(p(2, -8), p(7, 8), color);
        break;
    case Icon::ChevronRight:
        m_list->AddLine(p(-3, -7), p(4, 0), color, stroke);
        m_list->AddLine(p(4, 0), p(-3, 7), color, stroke);
        break;
    case Icon::ThumbUp:
    case Icon::ThumbUpFilled:
    case Icon::ThumbDown:
    case Icon::ThumbDownFilled: {
        // Drawn upright; the dislike glyph is the same hand mirrored vertically.
        const bool down = icon == Icon::ThumbDown || icon == Icon::ThumbDownFilled;
        const bool filled = icon == Icon::ThumbUpFilled || icon == Icon::ThumbDownFilled;
        const auto q = [&](float x, float y) { return p(down ? -x : x, down ? -y : y); };
        const std::array hand{q(-3, -1),   q(1, -9), q(3, -8), q(2.5f, -2.5f),
                              q(8, -2.5f), q(9, -1), q(7, 8),  q(-3, 8)};
        if (filled) {
            // The hand is concave where the thumb meets the palm: fill it as two convex parts.
            const std::array thumb{hand[0], hand[1], hand[2], hand[3]};
            const std::array palm{q(-3, -2.5f), hand[4], hand[5], hand[6], hand[7]};
            m_list->AddConvexPolyFilled(thumb.data(), static_cast<int>(thumb.size()), color);
            m_list->AddConvexPolyFilled(palm.data(), static_cast<int>(palm.size()), color);
            m_list->AddRectFilled(down ? q(-4, 8) : q(-9, -1), down ? q(-9, -1) : q(-4, 8), color);
        } else {
            m_list->AddPolyline(hand.data(), static_cast<int>(hand.size()), color, stroke,
                                ImDrawFlags_Closed);
            m_list->AddRect(down ? q(-4, 8) : q(-9, -1), down ? q(-9, -1) : q(-4, 8), color, 0,
                            stroke);
        }
        break;
    }
    case Icon::Comment:
        m_list->AddRect(p(-9, -8), p(9, 6), color, unit * 2, stroke);
        m_list->AddLine(p(-5, -3), p(5, -3), color, stroke);
        m_list->AddLine(p(-5, 1), p(2, 1), color, stroke);
        m_list->AddTriangleFilled(p(-5, 5), p(1, 5), p(-5, 10), color);
        break;
    case Icon::Send:
        m_list->AddTriangleFilled(p(-9, -8), p(10, 0), p(-9, 8), color);
        m_list->AddTriangleFilled(p(-9, -8), p(-5, 0), p(-9, 8), IM_COL32_BLACK);
        break;
    case Icon::Check:
        m_list->AddLine(p(-8, 0), p(-3, 6), color, stroke);
        m_list->AddLine(p(-3, 6), p(8, -6), color, stroke);
        break;
    case Icon::Add:
        m_list->AddLine(p(-8, 0), p(8, 0), color, stroke);
        m_list->AddLine(p(0, -8), p(0, 8), color, stroke);
        break;
    case Icon::SwitchAccount:
        m_list->AddCircle(p(-2, -3), unit * 4, color, 0, stroke);
        m_list->PathArcTo(p(-2, 10), unit * 8, std::numbers::pi_v<float> * 1.15f,
                          std::numbers::pi_v<float> * 1.85f);
        m_list->PathStroke(color, stroke);
        m_list->AddLine(p(6, -6), p(10, -2), color, stroke);
        m_list->AddLine(p(10, -2), p(6, 2), color, stroke);
        break;
    case Icon::Shorts: {
        // An upright screen with a play triangle (a generic shape, not the official logo).
        m_list->AddRect(p(-6, -9.5f), p(6, 9.5f), color, unit * 3, stroke);
        m_list->AddTriangleFilled(p(-2, -3.5f), p(-2, 3.5f), p(3.5f, 0), color);
        break;
    }
    case Icon::Link:
        // Two rounded links overlapping side by side.
        m_list->AddRect(p(-11, -4.5f), p(1.5f, 4.5f), color, unit * 4.5f, stroke);
        m_list->AddRect(p(-1.5f, -4.5f), p(11, 4.5f), color, unit * 4.5f, stroke);
        break;
    case Icon::Verified:
        m_list->AddCircleFilled(c, unit * 10, color);
        m_list->AddLine(p(-5, 0), p(-1.5f, 4), theme::kBackground, stroke * 1.2f);
        m_list->AddLine(p(-1.5f, 4), p(5.5f, -4), theme::kBackground, stroke * 1.2f);
        break;
    case Icon::Mic:
        // A capsule in a cradle on a stem.
        m_list->AddRectFilled(p(-3.5f, -10), p(3.5f, 2.5f), color, unit * 3.5f);
        m_list->PathArcTo(p(0, -1.5f), unit * 7, 0, std::numbers::pi_v<float>);
        m_list->PathStroke(color, stroke);
        m_list->AddLine(p(0, 5.5f), p(0, 10), color, stroke);
        break;
    case Icon::Bell: {
        // A bell: rounded dome over a flared rim, the clapper below.
        m_list->PathArcTo(p(0, -2), unit * 6, std::numbers::pi_v<float>,
                          2 * std::numbers::pi_v<float>);
        m_list->PathLineTo(p(6, 4));
        m_list->PathLineTo(p(8, 6.5f));
        m_list->PathLineTo(p(-8, 6.5f));
        m_list->PathLineTo(p(-6, 4));
        m_list->PathStroke(color, stroke, ImDrawFlags_Closed);
        m_list->AddLine(p(0, -8), p(0, -10), color, stroke);
        m_list->PathArcTo(p(0, 7.5f), unit * 2.5f, 0, std::numbers::pi_v<float>);
        m_list->PathStroke(color, stroke);
        break;
    }
    case Icon::ChevronDown:
        m_list->AddLine(p(-6, -3), p(0, 3), color, stroke);
        m_list->AddLine(p(0, 3), p(6, -3), color, stroke);
        break;
    case Icon::MoreVertical:
        for (const float y : {-6.0f, 0.0f, 6.0f}) {
            m_list->AddCircleFilled(p(0, y), unit * 2.1f, color);
        }
        break;
    case Icon::MoreHorizontal:
        for (const float x : {-6.0f, 0.0f, 6.0f}) {
            m_list->AddCircleFilled(p(x, 0), unit * 2.1f, color);
        }
        break;
    case Icon::History:
        m_list->AddCircle(c, unit * 8, color, 0, stroke);
        m_list->AddLine(c, p(0, -5), color, stroke);
        m_list->AddLine(c, p(4, 3), color, stroke);
        break;
    case Icon::Tune:
        m_list->AddLine(p(-9, -4), p(9, -4), color, stroke);
        m_list->AddLine(p(-9, 4), p(9, 4), color, stroke);
        m_list->AddCircleFilled(p(-3, -4), unit * 2.5f, color);
        m_list->AddCircleFilled(p(4, 4), unit * 2.5f, color);
        break;
    case Icon::InsertUp:
        m_list->AddLine(p(6, 6), p(-6, -6), color, stroke);
        m_list->AddLine(p(-6, -6), p(2, -6), color, stroke);
        m_list->AddLine(p(-6, -6), p(-6, 2), color, stroke);
        break;
    case Icon::Settings: {
        // Gear: a ring with eight square teeth.
        for (int tooth = 0; tooth < 8; ++tooth) {
            const float angle = static_cast<float>(tooth) * std::numbers::pi_v<float> / 4;
            const ImVec2 d{std::cos(angle), std::sin(angle)};
            m_list->AddLine(p(d.x * 6, d.y * 6), p(d.x * 9.5f, d.y * 9.5f), color, unit * 3.5f);
        }
        m_list->AddCircle(c, unit * 6.5f, color, 0, stroke);
        m_list->AddCircle(c, unit * 2.5f, color, 0, stroke);
        break;
    }
    case Icon::Fullscreen:
    case Icon::FullscreenExit: {
        // Four corner brackets, pointing outwards to enter and inwards to exit.
        const float in = icon == Icon::Fullscreen ? 1.0f : -1.0f;
        for (const float sx : {-1.0f, 1.0f}) {
            for (const float sy : {-1.0f, 1.0f}) {
                const ImVec2 corner =
                    icon == Icon::Fullscreen ? p(sx * 8, sy * 7) : p(sx * 3, sy * 2);
                const std::array points{
                    ImVec2{corner.x, corner.y - sy * in * unit * 5},
                    corner,
                    ImVec2{corner.x - sx * in * unit * 5, corner.y},
                };
                m_list->AddPolyline(points.data(), static_cast<int>(points.size()), color, stroke,
                                    ImDrawFlags_None);
            }
        }
        break;
    }
    case Icon::Captions:
    case Icon::CaptionsOn: {
        // "CC" in a rounded box; shown on, the box is filled and the letters cut out of it.
        const bool on = icon == Icon::CaptionsOn;
        ImU32 letters = color;
        if (on) {
            m_list->AddRectFilled(p(-10, -8), p(10, 8), color, unit * 2.5f);
            letters = color & ~IM_COL32(0xff, 0xff, 0xff, 0); // Black, same opacity.
        } else {
            m_list->AddRect(p(-10, -8), p(10, 8), color, unit * 2.5f, stroke);
        }
        for (const float x : {-4.0f, 4.5f}) {
            m_list->PathArcTo(p(x, 0), unit * 3.5f, std::numbers::pi_v<float> * 0.3f,
                              std::numbers::pi_v<float> * 1.7f);
            m_list->PathStroke(letters, stroke);
        }
        break;
    }
    case Icon::FastForward:
        m_list->AddTriangleFilled(p(-9, -6), p(-9, 6), p(0, 0), color);
        m_list->AddTriangleFilled(p(0, -6), p(0, 6), p(9, 0), color);
        break;
    case Icon::SkipPrevious:
    case Icon::SkipNext: {
        // A triangle against a bar, mirrored for previous.
        const float s = icon == Icon::SkipNext ? 1.0f : -1.0f;
        // Same winding both ways, as anti-aliased fills depend on it.
        m_list->AddTriangleFilled(p(s * -7, s * -8), p(s * -7, s * 8), p(s * 5, 0), color);
        const ImVec2 a = p(s * 5.5f, -8);
        const ImVec2 b = p(s * 8, 8);
        m_list->AddRectFilled({std::min(a.x, b.x), a.y}, {std::max(a.x, b.x), b.y}, color);
        break;
    }
    case Icon::Share: {
        // A curved arrow pointing right, as on the official share button.
        m_list->PathLineTo(p(-9, 8));
        m_list->PathBezierCubicCurveTo(p(-9, -1), p(-3, -3), p(4, -3));
        m_list->PathStroke(color, stroke);
        m_list->AddTriangleFilled(p(3, -9), p(10, -3), p(3, 3), color);
        break;
    }
    case Icon::Save:
    case Icon::SaveFilled: {
        // A bookmark: a rounded-top tab with a notch at the bottom.
        const std::array<ImVec2, 5> outline{p(-7, -9), p(7, -9), p(7, 9), p(0, 4), p(-7, 9)};
        if (icon == Icon::SaveFilled) {
            m_list->AddConcavePolyFilled(outline.data(), static_cast<int>(outline.size()), color);
        } else {
            m_list->AddPolyline(outline.data(), static_cast<int>(outline.size()), color, stroke,
                                ImDrawFlags_Closed);
        }
        break;
    }
    case Icon::Download:
    case Icon::Downloaded:
        if (icon == Icon::Downloaded) {
            m_list->AddCircleFilled(p(0, -2), unit * 8, color);
            m_list->AddLine(p(-4, -2), p(-1, 1), IM_COL32_BLACK, stroke);
            m_list->AddLine(p(-1, 1), p(4, -5), IM_COL32_BLACK, stroke);
        } else {
            m_list->AddLine(p(0, -9), p(0, 4), color, stroke);
            m_list->AddLine(p(-5, -1), p(0, 4), color, stroke);
            m_list->AddLine(p(5, -1), p(0, 4), color, stroke);
        }
        m_list->AddLine(p(-8, 9), p(8, 9), color, stroke);
        break;
    case Icon::CheckboxOff:
        m_list->AddRect(p(-8, -8), p(8, 8), color, unit * 2, stroke);
        break;
    case Icon::CheckboxOn:
        m_list->AddRectFilled(p(-9, -9), p(9, 9), color, unit * 2);
        m_list->AddLine(p(-5, 0), p(-1.5f, 4), IM_COL32_BLACK, stroke);
        m_list->AddLine(p(-1.5f, 4), p(5.5f, -4), IM_COL32_BLACK, stroke);
        break;
    }
}

} // namespace sloptube::app
