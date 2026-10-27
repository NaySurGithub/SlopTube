// Launch animation, after the official app's. Android's own splash screen shows the launcher icon
// until the first frame; the app then draws the same logo at the same place and shrinks it into
// the play glyph of a loading bar, which fills while the home feed loads. Nothing waits on a fixed
// delay: once the feed is in, the bar completes and everything fades out.

#include "app/App.hpp"
#include "app/ui/Logo.hpp"
#include "app/ui/LogoShape.hpp"
#include "app/ui/Motion.hpp"
#include "app/ui/Theme.hpp"

#include <algorithm>

namespace sloptube::app {
namespace {

// Size of the icon on Android's splash screen (no icon background), centred on the screen.
constexpr float kSystemIconSize = 288; // dp
// Loader, measured on the official app on the Galaxy A14 (dp, seconds).
constexpr float kPlayHeight = 16;
constexpr float kPlayGap = 7;
constexpr float kBarWidth = 190;
constexpr float kBarHeight = 4;
constexpr float kGlowWidth = 42;     // The fill's leading edge fades into the logo's second colour.
constexpr float kMorph = 0.45f;      // Logo to loader.
constexpr float kWaitingFill = 0.9f; // The bar creeps up to here until the feed is in.
constexpr float kWaitingRate = 1.2f;
constexpr float kFinishRate = 9;
constexpr float kTimeout = 8; // Offline or very slow: show the app (and its offline state).
constexpr float kFadeOut = 0.3f;
constexpr ImU32 kTrack = IM_COL32(0xca, 0xca, 0xca, 0xff);

ImVec2 lerp(ImVec2 a, ImVec2 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

} // namespace

void App::drawSplash(Frame& frame, ImVec2 display) {
    SplashState& s = m_splash;
    if (s.finished) {
        return;
    }
    if (s.startedAt < 0) {
        s.startedAt = frame.time;
    }
    frame.animating = true;

    const Canvas& c = frame.canvas;
    ImDrawList* list = c.list();
    const double elapsed = frame.time - s.startedAt;
    const Feed& home = *m_tabFeeds[std::to_underlying(Tab::Home)];
    const bool ready = home.loaded || !home.error.empty() || elapsed > kTimeout;
    if (s.doneAt < 0) {
        if (ready && s.barAt < 0) {
            s.doneAt = frame.time; // Already in: no loader at all.
        } else {
            if (s.barAt < 0) {
                s.barAt = frame.time;
            }
            motion::approach(s.progress, ready ? 1 : kWaitingFill, frame.seconds,
                             ready ? kFinishRate : kWaitingRate);
            if (ready && s.progress >= 1) {
                s.doneAt = frame.time;
            }
        }
    }
    const float fade =
        s.doneAt < 0 ? 1
                     : 1 - motion::easeOutCubic(motion::progress(frame.time, s.doneAt, kFadeOut));
    if (fade <= 0) {
        s.finished = true;
        return;
    }
    c.rect({0, 0}, display, motion::withAlpha(theme::kBackground, fade));

    // Loader layout: the play glyph, a gap and the bar, centred together.
    const ImVec2 center{display.x * 0.5f, display.y * 0.5f};
    const LogoBounds& play = logoPlayBounds();
    const ImVec2 playSize{play.max.x - play.min.x, play.max.y - play.min.y};
    const ImVec2 playCenter{(play.min.x + play.max.x) * 0.5f, (play.min.y + play.max.y) * 0.5f};
    const float smallScale = c.dp(kPlayHeight) / playSize.y;
    const float playWidth = playSize.x * smallScale;
    const float left = center.x - (playWidth + c.dp(kPlayGap + kBarWidth)) * 0.5f;

    // The logo, from where Android's splash screen left it to the loader's play glyph.
    const float morph =
        s.barAt < 0 ? 0 : motion::easeInOutCubic(motion::progress(frame.time, s.barAt, kMorph));
    const float bigScale = c.dp(kSystemIconSize) / logo::kViewport;
    const ImVec2 bigAt{center.x + (playCenter.x - logo::kViewport * 0.5f) * bigScale,
                       center.y + (playCenter.y - logo::kViewport * 0.5f) * bigScale};
    const ImVec2 smallAt{left + playWidth * 0.5f, center.y};
    drawLogo(list, lerp(bigAt, smallAt, morph), playCenter,
             bigScale + (smallScale - bigScale) * morph,
             LogoStyle{.alpha = fade, .dripsAlpha = std::max(0.0f, 1 - morph * 2), .white = morph});
    if (morph <= 0) {
        return;
    }

    const float shown = morph * fade;
    const float barLeft = left + playWidth + c.dp(kPlayGap);
    const float barRight = barLeft + c.dp(kBarWidth);
    const float half = c.dp(kBarHeight) * 0.5f;
    const float radius = kBarHeight * 0.5f;
    c.rect({barLeft, center.y - half}, {barRight, center.y + half},
           motion::withAlpha(kTrack, shown), radius);
    const float fill = barLeft + (barRight - barLeft) * s.progress;
    const float glow = std::max(barLeft, fill - c.dp(kGlowWidth));
    const ImU32 red = motion::withAlpha(theme::kAccent, shown);
    const ImU32 pink = motion::withAlpha(logo::kEndColor, shown);
    if (glow > barLeft) {
        c.rect({barLeft, center.y - half}, {glow + half, center.y + half}, red, radius);
    }
    if (fill > glow) {
        list->AddRectFilledMultiColor({glow, center.y - half}, {fill, center.y + half}, red, pink,
                                      pink, red);
    }
}

} // namespace sloptube::app
