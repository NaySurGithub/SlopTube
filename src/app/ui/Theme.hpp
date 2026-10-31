#pragma once

#include "app/ui/Fonts.hpp"

#include <imgui.h>

// Visual constants. Colours were sampled from reference screenshots of the official app's dark
// theme; dimensions are in density-independent pixels (dp) or scale-independent pixels (sp).
namespace sloptube::app::theme {

inline constexpr ImU32 kBackground = IM_COL32(0x0f, 0x0f, 0x0f, 0xff);
inline constexpr ImU32 kSurface = IM_COL32(0x27, 0x27, 0x27, 0xff);
inline constexpr ImU32 kSurfaceRaised = IM_COL32(0x21, 0x21, 0x21, 0xff); // Bottom sheets.
inline constexpr ImU32 kToast = IM_COL32(0xf1, 0xf1, 0xf1, 0xf0);
inline constexpr ImU32 kSurfacePressed = IM_COL32(0x3f, 0x3f, 0x3f, 0xff);
inline constexpr ImU32 kPlaceholder = IM_COL32(0x1f, 0x1f, 0x1f, 0xff);
inline constexpr ImU32 kTextPrimary = IM_COL32(0xf1, 0xf1, 0xf1, 0xff);
inline constexpr ImU32 kTextSecondary = IM_COL32(0xaa, 0xaa, 0xaa, 0xff);
inline constexpr ImU32 kTextOnLight = IM_COL32(0x0f, 0x0f, 0x0f, 0xff);
inline constexpr ImU32 kAccent = IM_COL32(0xff, 0x00, 0x33, 0xff);
inline constexpr ImU32 kLink = IM_COL32(0x3e, 0xa6, 0xff, 0xff);
// Measured on the official app (Galaxy A14): the unread count on the bell, and the 1 dp border of
// outlined buttons ("Accounts", the second profile button).
inline constexpr ImU32 kBadgeRed = IM_COL32(0xe1, 0x00, 0x2d, 0xff);
inline constexpr ImU32 kOutline = IM_COL32(0x4c, 0x4c, 0x4c, 0xff);
inline constexpr ImU32 kBadge = IM_COL32(0x00, 0x00, 0x00, 0xcc);
inline constexpr ImU32 kScrim = IM_COL32(0x00, 0x00, 0x00, 0x66);
inline constexpr ImU32 kDivider = IM_COL32(0xff, 0xff, 0xff, 0x1a);
inline constexpr ImU32 kTrack = IM_COL32(0xff, 0xff, 0xff, 0x4d);
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
