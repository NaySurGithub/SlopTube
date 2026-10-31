#pragma once

#include "app/ui/Fonts.hpp"

#include <imgui.h>

#include <cstdint>
#include <string_view>

namespace sloptube::app {

enum class Icon : std::uint8_t {
    Home,
    Subscriptions,
    Library,
    Search,
    Back,
    Close,
    Play,
    Pause,
    ChevronRight,
    ThumbUp,
    ThumbUpFilled,
    ThumbDown,
    ThumbDownFilled,
    Comment,
    Send,
    Check,
    Add,
    SwitchAccount,
    Shorts,
    Settings,
    Fullscreen,
    FullscreenExit,
    Captions,
    CaptionsOn,
    SkipPrevious,
    SkipNext,
    FastForward,
    Share,
    Save,
    SaveFilled,
    Download,
    Downloaded,
    CheckboxOff,
    CheckboxOn,
    History,        // A clock: past searches.
    Tune,           // Two sliders: search filters.
    InsertUp,       // An arrow to the top left: completes the query with a suggestion.
    Bell,           // Notifications.
    Mic,            // Voice search.
    ChevronDown,    // Opens a list ("Accounts").
    MoreVertical,   // ⋮ Actions of a video card.
    MoreHorizontal, // ••• Actions of a shelf tile or a list row.
};

// Horizontal placement of wrapped text in its box. Natural follows the paragraph direction: left
// for left-to-right text, right for right-to-left (Arabic, Hebrew, ...).
enum class TextAlign : std::uint8_t {
    Natural,
    Center,
};

// Density-aware drawing on top of an ImDrawList. Sizes passed in are dp (geometry) or sp (text).
class Canvas {
public:
    Canvas(ImDrawList* list, const Fonts& fonts, float density)
        : m_list(list)
        , m_fonts(fonts)
        , m_font(fonts.get(Weight::Regular))
        , m_density(density) {}

    // A copy drawing text in `weight`.
    [[nodiscard]] Canvas with(Weight weight) const;

    [[nodiscard]] ImDrawList* list() const { return m_list; }

    [[nodiscard]] float dp(float value) const { return value * m_density; }

    // Text in any script: right-to-left runs are shaped and reordered for display. `text` and
    // `measure` take UTF-8 in logical order, as received.
    [[nodiscard]] ImVec2 measure(float sizeSp, std::string_view text) const;
    void text(ImVec2 position, float sizeSp, ImU32 color, std::string_view text) const;

    // Word-wrapped text clamped to `maxLines`, the last visible line ending in an ellipsis (at its
    // logical end) when truncated. Returns the height consumed.
    float paragraph(ImVec2 position, float width, float sizeSp, ImU32 color, std::string_view text,
                    int maxLines, TextAlign align = TextAlign::Natural) const;

    // Lines `paragraph` would draw, without drawing; for laying out variable-height blocks.
    [[nodiscard]] int lineCount(float width, float sizeSp, std::string_view text,
                                int maxLines) const;

    // Single line truncated with an ellipsis to fit `width`.
    void line(ImVec2 position, float width, float sizeSp, ImU32 color, std::string_view text,
              TextAlign align = TextAlign::Natural) const;

    [[nodiscard]] float lineHeight(float sizeSp) const;

    void icon(Icon icon, ImVec2 center, float sizeDp, ImU32 color) const;
    void spinner(ImVec2 center, float sizeDp, ImU32 color, double time) const;
    void avatar(ImVec2 center, float sizeDp, std::string_view name) const;
    void rect(ImVec2 min, ImVec2 max, ImU32 color, float roundingDp = 0) const;

private:
    void draw(ImVec2 position, float size, ImU32 color, std::string_view text) const;

    ImDrawList* m_list;
    Fonts m_fonts;
    ImFont* m_font;
    float m_density;
};

} // namespace sloptube::app
