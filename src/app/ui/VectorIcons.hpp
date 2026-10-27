#pragma once

#include <GLES3/gl3.h>
#include <imgui.h>

#include <cstdint>

namespace sloptube::app {

// Icons of the navigation bar. Each is a signed distance field rasterised once, at start-up, into
// one texture at the exact pixel size it is drawn at, so it costs a single textured quad per frame
// and stays sharp and anti-aliased.
enum class Glyph : std::uint8_t {
    HomeOutline,
    HomeFilled,
    ShortsOutline,
    ShortsFilled,
    SubscriptionsOutline,
    SubscriptionsFilled,
    YouOutline,
    YouFilled,
    Create,
    Count,
};

class VectorIcons {
public:
    // `sizePx` is the side of every glyph's square, a 24-unit design grid.
    explicit VectorIcons(int sizePx);
    ~VectorIcons();
    VectorIcons(const VectorIcons&) = delete;
    VectorIcons& operator=(const VectorIcons&) = delete;

    // Draws `glyph` tinted with `color`, centred on `center`.
    void draw(ImDrawList* list, Glyph glyph, ImVec2 center, ImU32 color) const;

private:
    GLuint m_texture = 0;
    int m_size;
};

} // namespace sloptube::app
