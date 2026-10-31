#pragma once

#include <imgui.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

// Line breaking plus, for text containing right-to-left scripts, Arabic shaping and bidi
// reordering into drawable visual-order lines, cached across frames.
namespace sloptube::app::text {

inline constexpr std::string_view kEllipsis = "\xE2\x80\xA6"; // U+2026

// Breaks logical `text` into at most `maxLines` lines of `width` pixels at font size `size`.
// Calls `emit(std::string_view line, bool truncated)` per line; a truncated line is the last one,
// cut to leave room for kEllipsis, which the caller appends at its logical end.
template <typename Emit>
void wrapLines(ImFont* font, float size, std::string_view text, float width, int maxLines,
               Emit&& emit) {
    const char* cursor = text.data();
    const char* const end = text.data() + text.size();
    for (int lines = 0; cursor < end && lines < maxLines; ++lines) {
        const char* wrap = font->CalcWordWrapPosition(size, cursor, end, width);
        if (wrap == cursor) {
            // A single glyph wider than the line: take it anyway.
            do {
                ++wrap;
            } while (wrap < end && (static_cast<unsigned char>(*wrap) & 0xC0) == 0x80);
        }
        if (lines == maxLines - 1 && wrap < end) {
            const float ellipsis = font->CalcTextSizeA(size, FLT_MAX, 0, kEllipsis.data(),
                                                       kEllipsis.data() + kEllipsis.size())
                                       .x;
            // Measured up to the line break only: past it, ImGui would carry on with the next
            // lines and the "cut" line would hold the rest of the text.
            const char* lineEnd = std::find(cursor, end, '\n');
            const char* cut = cursor;
            font->CalcTextSizeA(size, width - ellipsis, 0, cursor, lineEnd, &cut);
            emit(std::string_view(cursor, cut), true);
            return;
        }
        emit(std::string_view(cursor, wrap), false);
        cursor = wrap;
        while (cursor < end && (*cursor == ' ' || *cursor == '\n')) {
            ++cursor;
        }
    }
}

struct VisualLine {
    std::string text; // Visual (left-to-right drawing) order, ellipsis included.
    float width = 0;
};

struct Paragraph {
    bool rtl = false; // Paragraph direction; right-to-left paragraphs align to the right.
    std::vector<VisualLine> lines;
};

// Shaped, wrapped and reordered `text`, or nullptr when it holds no right-to-left script and can
// be drawn as is. The pointer stays valid until the next call.
[[nodiscard]] const Paragraph* layoutBidi(ImFont* font, float size, std::string_view text,
                                          float width, int maxLines);

} // namespace sloptube::app::text
