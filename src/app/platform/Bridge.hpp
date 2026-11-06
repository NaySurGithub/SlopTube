#pragma once

#include <jni.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

// The native half of the Java bridge (android/java): events Java receives on native code's
// behalf, and string conversions both directions need.
namespace sloptube::app {

// The commands of PlaybackService.nativeMediaCommand, in the same order.
enum class MediaCommand : std::uint8_t {
    Play,
    Pause,
    Next,
    Previous,
    Seek,
    Stop,
};

// Of voice search, in the order of the VOICE_ constants of SlopTubeActivity.
enum class VoiceState : std::uint8_t {
    Listening, // The microphone is open.
    NoMatch,   // Nothing was understood.
    Denied,    // The microphone permission was refused.
    Failed,    // The recognition service failed.
    Stopped,   // The app left the screen while listening.
};

struct BridgeEvent {
    enum class Kind : std::uint8_t {
        OpenVideo,                 // A notification was tapped.
        PictureInPicture,          // The activity entered or left picture-in-picture.
        PictureInPictureDismissed, // The user closed the picture-in-picture window.
        Media,                     // Notification, lock screen, headset or Bluetooth control.
        UpdateFailed,              // Installing an update failed or was declined.
        VoiceSearch,               // Android's speech recognizer heard a query.
        VoicePartial,              // The words heard so far, while listening.
        VoiceLevel,                // The microphone's sound level, while listening.
        VoiceState,                // The listening screen moved on (VoiceState).
    };

    Kind kind = Kind::Media;
    std::string videoId;                       // OpenVideo.
    bool active = false;                       // PictureInPicture.
    MediaCommand command = MediaCommand::Play; // Media.
    std::int64_t positionMs = 0;               // Media: Seek.
    std::string reason;                        // UpdateFailed, as Android words it.
    std::string query;                         // VoiceSearch, VoicePartial.
    float levelDb = 0;                         // VoiceLevel, as SpeechRecognizer reports it.
    VoiceState voice = VoiceState::Listening;  // VoiceState.
};

// Events arrive on Java threads, so `handler` must be thread-safe. Events sent while no handler
// is set (before the app starts) wait for the next one.
void setBridgeHandler(std::function<void(BridgeEvent)> handler);

// NewStringUTF takes modified UTF-8, which emoji (four-byte sequences) break.
[[nodiscard]] jstring javaString(JNIEnv* env, std::string_view utf8);
[[nodiscard]] std::string nativeString(JNIEnv* env, jstring text);

} // namespace sloptube::app
