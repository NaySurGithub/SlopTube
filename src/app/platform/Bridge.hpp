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

struct BridgeEvent {
    enum class Kind : std::uint8_t {
        OpenVideo,                 // A notification was tapped.
        PictureInPicture,          // The activity entered or left picture-in-picture.
        PictureInPictureDismissed, // The user closed the picture-in-picture window.
        Media,                     // Notification, lock screen, headset or Bluetooth control.
        UpdateFailed,              // Installing an update failed or was declined.
    };

    Kind kind = Kind::Media;
    std::string videoId;                       // OpenVideo.
    bool active = false;                       // PictureInPicture.
    MediaCommand command = MediaCommand::Play; // Media.
    std::int64_t positionMs = 0;               // Media: Seek.
    std::string reason;                        // UpdateFailed, as Android words it.
};

// Events arrive on Java threads, so `handler` must be thread-safe. Events sent while no handler
// is set (before the app starts) wait for the next one.
void setBridgeHandler(std::function<void(BridgeEvent)> handler);

// NewStringUTF takes modified UTF-8, which emoji (four-byte sequences) break.
[[nodiscard]] jstring javaString(JNIEnv* env, std::string_view utf8);
[[nodiscard]] std::string nativeString(JNIEnv* env, jstring text);

} // namespace sloptube::app
