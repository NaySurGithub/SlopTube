// Voice search: a screen over everything while Android's speech recognizer listens, laid out as
// the official app's: back at the top left, the words in large type as they are heard, and the
// microphone at the bottom, ringed by the sound level.

#include "app/App.hpp"
#include "app/ui/Motion.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace sloptube::app {
namespace {

// As measured in the official app's layout (dp, sp).
constexpr float kBackX = 34;         // Centre of the back arrow: 10 margin, 24 half button.
constexpr float kBackY = 36;         // 12 margin, 24 half button.
constexpr float kTextTop = 102;      // Under the back button, 18 below its row.
constexpr float kTextInset = 24;     // 12 margin and 12 padding.
constexpr float kLargeTextSize = 24; // sp, bold.
constexpr float kHintTextSize = 16;  // sp
constexpr int kTextLines = 5;
constexpr float kMicSize = 72;        // The microphone's disc.
constexpr float kMicBottom = 26 + 48; // Its margin, above the navigation area.
constexpr float kRingGrowth = 0.6f;   // The level ring, at full level, past the disc's radius.
constexpr float kHintGap = 26;        // Between the hint and the disc.
// SpeechRecognizer's levels (dB) span about -2 (silence) to 10 (a raised voice).
constexpr float kQuietDb = -2;
constexpr float kLoudDb = 10;
constexpr float kLevelEase = 18; // Per second, towards the reported level.

} // namespace

void App::openVoiceSearch() {
    if (m_voice.open || !m_platform.startVoiceSearch()) {
        return; // Without a recognition service, Android's own screen opened instead.
    }
    m_voice = VoiceSearchState{.open = true};
    ImGui::ClearActiveID(); // Dismisses the keyboard.
    // The microphone should not hear the video.
    if (m_player.state() == media::PlayerState::Playing) {
        m_player.setPaused(true);
        m_voice.resumePlayback = true;
    }
}

void App::closeVoiceSearch() {
    if (!m_voice.open) {
        return;
    }
    m_platform.cancelVoiceSearch();
    if (m_voice.resumePlayback) {
        m_player.setPaused(false);
    }
    m_voice = {};
}

void App::onVoiceEvent(const BridgeEvent& event) {
    using Phase = VoiceSearchState::Phase;
    switch (event.kind) {
    case BridgeEvent::Kind::VoiceSearch:
        // Also the query of Android's own screen, when it was used instead.
        if (m_voice.open) {
            m_voice.heard = event.query;
            closeVoiceSearch();
        }
        searchFor(event.query);
        return;
    case BridgeEvent::Kind::VoicePartial:
        if (m_voice.open) {
            m_voice.heard = event.query;
            m_voice.phase = Phase::Listening;
        }
        return;
    case BridgeEvent::Kind::VoiceLevel:
        m_voice.levelDb = event.levelDb;
        return;
    case BridgeEvent::Kind::VoiceState:
        if (!m_voice.open) {
            return;
        }
        switch (event.voice) {
        case VoiceState::Listening:
            m_voice.phase = Phase::Listening;
            break;
        case VoiceState::NoMatch:
            m_voice.phase = Phase::NoMatch;
            break;
        case VoiceState::Denied:
            m_voice.phase = Phase::Denied;
            break;
        case VoiceState::Failed:
            m_voice.phase = Phase::Failed;
            break;
        case VoiceState::Stopped:
            closeVoiceSearch();
            break;
        }
        if (m_voice.phase != Phase::Listening) {
            m_voice.levelDb = kQuietDb;
        }
        return;
    default:
        return;
    }
}

void App::drawVoiceSearch(Frame& frame, ImVec2 display) {
    using Phase = VoiceSearchState::Phase;
    VoiceSearchState& v = m_voice;
    const Canvas& c = frame.canvas;
    const ImVec2 min{0, m_insets.top};
    const ImVec2 max{display.x, display.y - m_insets.bottom};
    c.rect({0, 0}, display, theme::g_background);

    if (iconButton(frame, Icon::Back, {min.x + c.dp(kBackX), min.y + c.dp(kBackY)},
                   theme::g_textPrimary)) {
        later([this] { closeVoiceSearch(); });
    }

    // The words as they come, or what to do next.
    const bool listening = v.phase == Phase::Starting || v.phase == Phase::Listening;
    const float textWidth = max.x - min.x - c.dp(kTextInset) * 2;
    if (listening) {
        const bool heard = !v.heard.empty();
        c.with(theme::kHeadingWeight)
            .paragraph({min.x + c.dp(kTextInset), min.y + c.dp(kTextTop)}, textWidth,
                       kLargeTextSize, heard ? theme::g_textPrimary : theme::g_textSecondary,
                       heard ? std::string_view(v.heard) : tr(Text::VoiceListening), kTextLines);
    }

    // The level eases towards the last reported one, so the ring breathes rather than flickers.
    const float target =
        listening ? std::clamp((v.levelDb - kQuietDb) / (kLoudDb - kQuietDb), 0.0f, 1.0f) : 0;
    v.level += (target - v.level) * std::min(1.0f, frame.seconds * kLevelEase);

    const float radius = c.dp(kMicSize) * 0.5f;
    const ImVec2 mic{(min.x + max.x) * 0.5f, max.y - c.dp(kMicBottom) - radius};
    if (listening) {
        c.list()->AddCircleFilled(mic, radius * (1 + kRingGrowth * v.level),
                                  motion::withAlpha(theme::kAccent, 0.25f));
    }
    c.list()->AddCircleFilled(mic, radius, listening ? theme::kAccent : theme::g_surface);
    c.icon(Icon::Mic, mic, theme::kIconSize * 1.5f,
           listening ? IM_COL32_WHITE : theme::g_textPrimary);

    if (!listening) {
        const Text hint = v.phase == Phase::NoMatch  ? Text::VoiceNoMatch
                          : v.phase == Phase::Denied ? Text::VoiceDenied
                                                     : Text::VoiceFailed;
        const float hintHeight = c.lineHeight(kHintTextSize) * 3;
        c.paragraph({min.x + c.dp(kTextInset), mic.y - radius - c.dp(kHintGap) - hintHeight},
                    textWidth, kHintTextSize, theme::g_textSecondary, tr(hint), 3,
                    TextAlign::Center);
    }

    // Listening, a tap ends it early and searches what was heard; otherwise it listens again.
    if (frame.touch.tapped({mic.x - radius, mic.y - radius}, {mic.x + radius, mic.y + radius})) {
        if (listening) {
            later([this] { m_platform.finishVoiceSearch(); });
        } else {
            later([this] {
                if (m_platform.startVoiceSearch()) {
                    m_voice.phase = VoiceSearchState::Phase::Starting;
                    m_voice.heard.clear();
                } else {
                    closeVoiceSearch();
                }
            });
        }
    }
    frame.animating = true;
}

} // namespace sloptube::app
