#pragma once

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace sloptube::app {

// Roboto's weights, as the official app uses them (400, 500, 600 and 700).
enum class Weight : std::uint8_t {
    Regular,
    Medium,
    SemiBold,
    Bold,
};

inline constexpr std::size_t kWeightCount = 4;

// Sizes are Android text sizes: the em square, not ImGui's ascender-to-descender height.
struct Fonts {
    std::array<ImFont*, kWeightCount> faces{};
    // Baseline to baseline, in ems: Android's default line height for the primary face.
    float lineSpacing = 1;

    [[nodiscard]] ImFont* get(Weight weight) const {
        return faces[static_cast<std::size_t>(weight)];
    }
};

// A colour emoji rasterised by the platform, top-left at the line's top.
struct EmojiGlyph {
    int width = 0;
    int height = 0;
    float baseline = 0; // Distance from the bitmap's top to the text baseline.
    float advance = 0;
    std::vector<std::uint32_t> pixels; // RGBA, straight alpha, row by row.
};

// Colour emoji come from the platform: ImGui's rasteriser cannot read colour fonts.
struct EmojiSource {
    std::function<bool(char32_t)> has;
    std::function<std::optional<EmojiGlyph>(char32_t, float textSizePx)> draw;
};

// Adds the UI fonts (Roboto in every Weight) to `atlas`, each with every installed system font for
// other scripts merged in as fallbacks, so text in any language finds a glyph. Files are
// memory-mapped once per process and glyphs are rasterised on demand, so large fonts (CJK) cost
// address space, not startup time. `language`/`region` pick the matching CJK face. Falls back to
// ImGui's built-in font. Emoji are drawn by `emoji`, which must outlive the atlas.
Fonts loadFonts(ImFontAtlas& atlas, float sizePx, std::string_view language,
                std::string_view region, const EmojiSource& emoji);

} // namespace sloptube::app
