// Vertical swipes on the player. Swiping the video up goes fullscreen and swiping down in
// fullscreen leaves it, as in the official app. In fullscreen, the left third sets the screen's
// brightness and the right third the media volume (Settings::playerSwipes), with the level shown
// at the top while it changes. Drags that are not clearly vertical, start on the progress bar's
// band or come while the 2x hold, seeking or a menu owns the player are left to them.

#include "app/App.hpp"
#include "app/ui/Motion.hpp"
#include "app/ui/Theme.hpp"

#include <algorithm>
#include <cmath>

namespace sloptube::app {
namespace {

constexpr float kVerticalRatio = 1.5f;    // Vertical travel over horizontal, to count as vertical.
constexpr float kTopInset = 48;           // dp: the status bar's swipe, and the top buttons.
constexpr float kFullscreenTravel = 48;   // dp to swipe for fullscreen.
constexpr float kFullLevelTravel = 0.75f; // Of the video's height, from 0 to 100 %.
constexpr float kLevelStep = 0.01f;       // Changes smaller than this are not sent to the system.
constexpr double kLevelShown = 0.6;       // Seconds the level stays after the last change.
// The level, as a pill at the top: icon, then a bar.
constexpr float kLevelTop = 24;    // dp
constexpr float kLevelWidth = 168; // dp
constexpr float kLevelHeight = 36; // dp
constexpr float kLevelIcon = 20;   // dp
constexpr float kLevelTrack = 4;   // dp

} // namespace

bool App::playerSwipes(Frame& frame, ImVec2 min, ImVec2 max) {
    using Kind = SwipeState::Kind;
    SwipeState& s = m_swipe;
    Touch& touch = frame.touch;
    if (!touch.down()) {
        s.kind = Kind::Undecided;
        return false;
    }
    const ImVec2 travel{touch.position().x - touch.start().x, touch.position().y - touch.start().y};

    if (s.kind == Kind::Undecided) {
        if (!touch.dragging()) {
            return false; // Not past the slop yet: maybe a tap, a hold or a pinch.
        }
        const bool free = m_watch.menu == PlayerMenu::None && m_sheet.kind == Sheet::None &&
                          !m_speedHeld && !m_watch.seek && !m_watch.fine;
        const bool vertical = std::abs(travel.y) > kVerticalRatio * std::abs(travel.x);
        const bool inside = touch.startedIn({min.x, min.y + frame.dp(kTopInset)},
                                            {max.x, max.y - frame.dp(theme::kScrubZone)});
        s.kind = Kind::Ignored;
        if (!free || !vertical || !inside) {
            return false;
        }
        const float third = (max.x - min.x) / 3;
        const float x = touch.start().x;
        if (!m_fullscreen) {
            // Down is the mini player's pull; up goes fullscreen.
            s.kind = travel.y < 0 ? Kind::Fullscreen : Kind::Ignored;
        } else if (m_settings.playerSwipes && x < min.x + third) {
            s.kind = Kind::Brightness;
            s.from = m_platform.windowBrightness();
        } else if (m_settings.playerSwipes && x > max.x - third) {
            s.kind = Kind::Volume;
            s.from = m_platform.musicVolume();
        } else {
            s.kind = travel.y > 0 ? Kind::Fullscreen : Kind::Ignored;
        }
        s.level = s.from;
        s.applied = s.from;
    }

    switch (s.kind) {
    case Kind::Undecided:
    case Kind::Ignored:
        return false;
    case Kind::Fullscreen:
        if (std::abs(travel.y) >= frame.dp(kFullscreenTravel)) {
            s.kind = Kind::Ignored; // Once per gesture.
            later([this] {
                setFullscreen(!m_fullscreen);
                m_platform.haptic(Haptic::Click);
            });
        }
        return true;
    case Kind::Brightness:
    case Kind::Volume:
        break;
    }

    // Swiping up raises the level; three quarters of the video's height spans all of it.
    s.level = std::clamp(s.from - travel.y / ((max.y - min.y) * kFullLevelTravel), 0.0f, 1.0f);
    if (std::abs(s.level - s.applied) >= kLevelStep ||
        (s.level != s.applied && (s.level == 0 || s.level == 1))) {
        s.applied = s.level;
        if (s.kind == Kind::Brightness) {
            m_platform.setWindowBrightness(s.level);
            s.brightnessSet = true;
        } else {
            m_platform.setMusicVolume(s.level);
        }
    }
    s.shown = s.kind;
    s.changedAt = frame.time;
    m_watch.controlsVisible = false;
    frame.animating = true;
    return true;
}

void App::drawSwipeLevel(Frame& frame, ImVec2 min, ImVec2 max) {
    using Kind = SwipeState::Kind;
    const SwipeState& s = m_swipe;
    if (s.shown != Kind::Brightness && s.shown != Kind::Volume) {
        return;
    }
    const bool held = s.kind == s.shown;
    const double age = frame.time - s.changedAt;
    if (!held && age > kLevelShown + motion::kFadeDuration) {
        return;
    }
    const float alpha =
        held || age < kLevelShown
            ? 1.0f
            : 1.0f - static_cast<float>((age - kLevelShown) / motion::kFadeDuration);
    frame.animating = true;

    const Canvas& c = frame.canvas;
    const float width = c.dp(kLevelWidth);
    const ImVec2 pillMin{(min.x + max.x - width) * 0.5f, min.y + c.dp(kLevelTop)};
    const ImVec2 pillMax{pillMin.x + width, pillMin.y + c.dp(kLevelHeight)};
    c.rect(pillMin, pillMax, motion::withAlpha(theme::kBadge, alpha), theme::kPillRadius);
    const float centerY = (pillMin.y + pillMax.y) * 0.5f;
    const float gutter = c.dp(theme::kGutter);
    const ImU32 ink = motion::withAlpha(IM_COL32_WHITE, alpha);
    c.icon(s.shown == Kind::Brightness ? Icon::Brightness : Icon::Volume,
           {pillMin.x + gutter + c.dp(kLevelIcon) * 0.5f, centerY}, kLevelIcon, ink);
    const float trackLeft = pillMin.x + gutter * 2 + c.dp(kLevelIcon);
    const float trackRight = pillMax.x - gutter;
    const float half = c.dp(kLevelTrack) * 0.5f;
    c.rect({trackLeft, centerY - half}, {trackRight, centerY + half},
           motion::withAlpha(theme::kOverlayButton, alpha), kLevelTrack * 0.5f);
    c.rect({trackLeft, centerY - half},
           {trackLeft + (trackRight - trackLeft) * s.level, centerY + half}, ink,
           kLevelTrack * 0.5f);
}

} // namespace sloptube::app
