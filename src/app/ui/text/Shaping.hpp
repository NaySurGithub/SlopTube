#pragma once

#include <string>

// Arabic contextual shaping onto the Unicode presentation forms (U+FB50-FDFF, U+FE70-FEFF), which
// fonts without an OpenType shaper can draw directly.
namespace sloptube::app::text {

// A presentation form (LAM WITH ALEF, isolated) whose presence in the font enables shaping.
inline constexpr char32_t kPresentationFormProbe = 0xFEFB;

// Shaping is skipped when the loaded fonts lack the presentation forms (base letters still draw).
void setArabicShapingEnabled(bool enabled);

// Replaces Arabic letters in logical-order `text` with their joined forms, including lam-alef
// ligatures. Returns false when nothing changed.
bool shapeArabic(std::u32string& text);

} // namespace sloptube::app::text
