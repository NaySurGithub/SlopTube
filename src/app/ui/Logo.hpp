#pragma once

#include <imgui.h>

// SlopTube's logo (android/logo.svg) drawn with ImGui, for the launch animation.
namespace sloptube::app {

struct LogoStyle {
    float alpha = 1;
    float dripsAlpha = 1; // The drips fade on their own while the logo turns into the loader.
    float white = 0;      // 0: the logo's gradient, 1: plain white.
};

// Draws the logo so that the point `anchor` (viewport units, 0-108) lands on `at`, at `scale`
// pixels per unit.
void drawLogo(ImDrawList* list, ImVec2 at, ImVec2 anchor, float scale, LogoStyle style = {});

// Bounds of the play glyph, stroke included, in viewport units.
struct LogoBounds {
    ImVec2 min;
    ImVec2 max;
};

[[nodiscard]] const LogoBounds& logoPlayBounds();

} // namespace sloptube::app
