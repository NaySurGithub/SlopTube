#pragma once

#include "app/ui/Fonts.hpp"

#include <imgui.h>

// Visual constants. The dark colours were sampled from reference screenshots of the official app's
// dark theme; dimensions are in density-independent pixels (dp) or scale-independent pixels (sp).
namespace sloptube::app::theme {

// The colours that change with the appearance.
struct Palette {
    ImU32 background;
    ImU32 surface;
    ImU32 surfaceRaised; // Bottom sheets.
    ImU32 toast;
    ImU32 surfacePressed;
    ImU32 placeholder;
    ImU32 textPrimary;
    ImU32 textSecondary;
    ImU32 textOnLight; // Over a fill of textPrimary or toast.
    ImU32 link;
    // Measured on the official app (Galaxy A14): the 1 dp border of outlined buttons ("Accounts",
    // the second profile button).
    ImU32 outline;
    ImU32 divider;
    ImU32 track;
    ImU32 ripple;
    ImU32 shimmer;
    ImU32 sheetHandle;
};

// The packaging script reads `background` here for the colour of the Android window.
inline constexpr Palette kDarkPalette{
    .background = IM_COL32(0x0f, 0x0f, 0x0f, 0xff),
    .surface = IM_COL32(0x27, 0x27, 0x27, 0xff),
    .surfaceRaised = IM_COL32(0x21, 0x21, 0x21, 0xff),
    .toast = IM_COL32(0xf1, 0xf1, 0xf1, 0xf0),
    .surfacePressed = IM_COL32(0x3f, 0x3f, 0x3f, 0xff),
    .placeholder = IM_COL32(0x1f, 0x1f, 0x1f, 0xff),
    .textPrimary = IM_COL32(0xf1, 0xf1, 0xf1, 0xff),
    .textSecondary = IM_COL32(0xaa, 0xaa, 0xaa, 0xff),
    .textOnLight = IM_COL32(0x0f, 0x0f, 0x0f, 0xff),
    .link = IM_COL32(0x3e, 0xa6, 0xff, 0xff),
    .outline = IM_COL32(0x4c, 0x4c, 0x4c, 0xff),
    .divider = IM_COL32(0xff, 0xff, 0xff, 0x1a),
    .track = IM_COL32(0xff, 0xff, 0xff, 0x4d),
    .ripple = IM_COL32(0xff, 0xff, 0xff, 0x24),
    .shimmer = IM_COL32(0xff, 0xff, 0xff, 0x10),
    .sheetHandle = IM_COL32(0xff, 0xff, 0xff, 0x40),
};

// The official app's light theme, from its colour resources (yt_sys_color_baseline_light_*):
// base and raised backgrounds white, the additive background (5 % black) under chips and fields,
// text #0f0f0f and #606060, call to action #065fd4, outline 10 % black.
inline constexpr Palette kLightPalette{
    .background = IM_COL32(0xff, 0xff, 0xff, 0xff),
    .surface = IM_COL32(0xf2, 0xf2, 0xf2, 0xff),
    .surfaceRaised = IM_COL32(0xff, 0xff, 0xff, 0xff),
    .toast = IM_COL32(0x0f, 0x0f, 0x0f, 0xf0),
    .surfacePressed = IM_COL32(0xe5, 0xe5, 0xe5, 0xff),
    .placeholder = IM_COL32(0xf2, 0xf2, 0xf2, 0xff),
    .textPrimary = IM_COL32(0x0f, 0x0f, 0x0f, 0xff),
    .textSecondary = IM_COL32(0x60, 0x60, 0x60, 0xff),
    .textOnLight = IM_COL32(0xff, 0xff, 0xff, 0xff),
    .link = IM_COL32(0x06, 0x5f, 0xd4, 0xff),
    .outline = IM_COL32(0x00, 0x00, 0x00, 0x1a),
    .divider = IM_COL32(0x00, 0x00, 0x00, 0x1a),
    .track = IM_COL32(0x00, 0x00, 0x00, 0x33),
    .ripple = IM_COL32(0x00, 0x00, 0x00, 0x1a),
    .shimmer = IM_COL32(0x00, 0x00, 0x00, 0x0c),
    .sheetHandle = IM_COL32(0x00, 0x00, 0x00, 0x33),
};

// The colours in use, dark until apply() says otherwise. Written once per change, on the main
// thread, and read like constants everywhere else.
inline ImU32 g_background = kDarkPalette.background;
inline ImU32 g_surface = kDarkPalette.surface;
inline ImU32 g_surfaceRaised = kDarkPalette.surfaceRaised;
inline ImU32 g_toast = kDarkPalette.toast;
inline ImU32 g_surfacePressed = kDarkPalette.surfacePressed;
inline ImU32 g_placeholder = kDarkPalette.placeholder;
inline ImU32 g_textPrimary = kDarkPalette.textPrimary;
inline ImU32 g_textSecondary = kDarkPalette.textSecondary;
inline ImU32 g_textOnLight = kDarkPalette.textOnLight;
inline ImU32 g_link = kDarkPalette.link;
inline ImU32 g_outline = kDarkPalette.outline;
inline ImU32 g_divider = kDarkPalette.divider;
inline ImU32 g_track = kDarkPalette.track;
inline ImU32 g_ripple = kDarkPalette.ripple;
inline ImU32 g_shimmer = kDarkPalette.shimmer;
inline ImU32 g_sheetHandle = kDarkPalette.sheetHandle;
inline bool g_isDark = true;

inline void apply(bool dark) {
    const Palette& palette = dark ? kDarkPalette : kLightPalette;
    g_isDark = dark;
    g_background = palette.background;
    g_surface = palette.surface;
    g_surfaceRaised = palette.surfaceRaised;
    g_toast = palette.toast;
    g_surfacePressed = palette.surfacePressed;
    g_placeholder = palette.placeholder;
    g_textPrimary = palette.textPrimary;
    g_textSecondary = palette.textSecondary;
    g_textOnLight = palette.textOnLight;
    g_link = palette.link;
    g_outline = palette.outline;
    g_divider = palette.divider;
    g_track = palette.track;
    g_ripple = palette.ripple;
    g_shimmer = palette.shimmer;
    g_sheetHandle = palette.sheetHandle;
}

// While alive (and `active`), the dark colours are in use whatever the appearance: for what is
// drawn over video, which stays dark as in the official app.
class DarkScope {
public:
    explicit DarkScope(bool active = true)
        : m_wasDark(g_isDark) {
        if (active && !g_isDark) {
            apply(true);
        }
    }

    ~DarkScope() {
        if (g_isDark != m_wasDark) {
            apply(m_wasDark);
        }
    }

    DarkScope(const DarkScope&) = delete;
    DarkScope& operator=(const DarkScope&) = delete;

private:
    bool m_wasDark;
};

// The same in both appearances: these sit over images and video.
inline constexpr ImU32 kAccent = IM_COL32(0xff, 0x00, 0x33, 0xff);
// Measured on the official app (Galaxy A14): the unread count on the bell.
inline constexpr ImU32 kBadgeRed = IM_COL32(0xe1, 0x00, 0x2d, 0xff);
inline constexpr ImU32 kBadge = IM_COL32(0x00, 0x00, 0x00, 0xcc);
inline constexpr ImU32 kScrim = IM_COL32(0x00, 0x00, 0x00, 0x66);
// Round buttons and labels over the video (the official app's white at 30 %).
inline constexpr ImU32 kOverlayButton = IM_COL32(0xff, 0xff, 0xff, 0x4d);

// Layout (dp).
inline constexpr float kTopBarHeight = 48;
inline constexpr float kBottomBarHeight = 48;
inline constexpr float kGutter = 12;
inline constexpr float kAvatarSize = 36;
inline constexpr float kIconSize = 24;
inline constexpr float kIconStroke = 2;
inline constexpr float kCornerRadius = 8;
inline constexpr float kCardRadius = 12;
inline constexpr float kSheetRadius = 12;
inline constexpr float kSmallAvatarSize = 24;
inline constexpr float kLargeAvatarSize = 72;
inline constexpr float kChipHeight = 32;
inline constexpr float kPillRadius = 18;
inline constexpr float kCardSpacing = 16;
inline constexpr float kRowThumbnailWidth = 160;
inline constexpr float kShelfThumbnailWidth = 144; // As measured on the official app.
inline constexpr float kShelfSpacing = 16;         // dp between tiles, and before the first.
inline constexpr float kBadgeMargin = 6;
inline constexpr float kBadgePadding = 4;
inline constexpr float kSearchFieldHeight = 36;
inline constexpr float kButtonHeight = 36;
inline constexpr float kProgressHeight = 3;
inline constexpr float kProgressHandle = 6;
// Seeking along a progress bar, measured on the official app: a touch band over the bar and a
// 20 dp red handle under the finger.
inline constexpr float kScrubZone = 32;
inline constexpr float kScrubHandle = 10; // Radius.
inline constexpr float kPlayButtonSize = 56;
inline constexpr float kTouchSlop = 8;
inline constexpr float kSpinnerSize = 28;

// Typography (sp), measured on the official app, whose text is the system Roboto: channel names
// 24 Bold; sheet titles 20 Bold; shelf headers and the watch title 18 Bold; card titles 16
// SemiBold with their metadata 12 Medium; buttons, chips and names 14 Medium; body text 14 and
// other metadata 12 Regular; navigation labels 10 Regular.
inline constexpr float kWordmarkSize = 20;
inline constexpr float kHeadlineSize = 24;
inline constexpr float kSheetTitleSize = 20;
inline constexpr float kSectionSize = 18;
inline constexpr float kWatchTitleSize = 18;
inline constexpr float kTitleSize = 16;
inline constexpr float kBodySize = 14;
inline constexpr float kCaptionSize = 12;
inline constexpr float kNavLabelSize = 10;
inline constexpr float kCodeSize = 32;

inline constexpr Weight kHeadingWeight = Weight::Bold;   // Headline, sheet, section, watch title.
inline constexpr Weight kTitleWeight = Weight::SemiBold; // Card titles.
inline constexpr Weight kLabelWeight = Weight::Medium;   // Buttons, chips, names.

inline constexpr float kAspect16x9 = 9.0f / 16.0f;

// Motion.
inline constexpr float kScrollFriction = 4.0f;  // Exponential velocity decay per second.
inline constexpr float kScrollStopSpeed = 20;   // dp/s below which a fling stops.
inline constexpr float kControlsTimeout = 3.0f; // Seconds before player controls auto-hide.
inline constexpr double kDoubleTap = 0.3;       // Seconds between the taps of a double tap.

} // namespace sloptube::app::theme
