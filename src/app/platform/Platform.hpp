#pragma once

#include "app/ui/Fonts.hpp"
#include "core/innertube/Api.hpp"

#include <jni.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

struct android_app;
struct AInputEvent;
struct ASensor;
struct ASensorEventQueue;

namespace sloptube::app {

// The vibrations of the official app's player, each made the way it makes it.
enum class Haptic : std::uint8_t {
    LongPress,    // The system's long-press feedback: holding for 2x.
    ContextClick, // The system's context click: double-tap like on a short.
    Click,        // VibrationEffect.EFFECT_CLICK: zoom settling, seek undo.
    Tick,         // VibrationEffect.EFFECT_TICK: zoom reaching fill, crossing a chapter.
    Seek,         // A 25 ms pulse at full strength: precise seeking, seeking from the video.
};

struct Insets {
    float top = 0;
    float bottom = 0;
    float left = 0;
    float right = 0;
};

// Access to Android framework services through JNI. All methods must be called on the thread
// that runs android_main.
class Platform {
public:
    explicit Platform(android_app* app);
    ~Platform();

    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    void setKeyboardVisible(bool visible);
    [[nodiscard]] char32_t unicodeChar(const AInputEvent* keyEvent);
    // Areas covered by visible system bars and display cutouts, in pixels.
    [[nodiscard]] Insets systemBarInsets();
    // Landscape with system bars hidden (swiping an edge shows them briefly). Leaving it returns
    // to portrait, and the orientation follows the device again once it is held upright.
    void setFullscreen(bool fullscreen);
    void openUrl(std::string_view url);
    // Opens the system share sheet for a plain-text message (a video link).
    void share(std::string_view subject, std::string_view text);
    // System feedback (long press, context click) follows the user's touch feedback setting;
    // the others go to the vibrator, as in the official app.
    void haptic(Haptic haptic);

    // The media notification, lock screen and headset controls (PlaybackService): the open video
    // and where it stands. `durationMs` is 0 for a live stream.
    struct Playback {
        std::string videoId;
        std::string title;
        std::string channel;
        std::int64_t durationMs = 0;
        std::int64_t positionMs = 0;
        bool playing = false;
        bool buffering = false;
        float speed = 1;
        bool hasPrevious = false;
        bool hasNext = false;
    };

    void updatePlayback(const Playback& playback);
    void stopPlayback();
    // Whether leaving the app turns the video of `width` x `height` into a picture-in-picture
    // window.
    void setPictureInPicture(bool allowed, int width, int height);
    // Schedules or cancels the hourly check for new uploads, asking for the permission to notify.
    void setUploadNotifications(bool enabled);
    // Hands the downloaded APK at `apk` to Android's installer, which asks the user to confirm.
    void installUpdate(const std::filesystem::path& apk);

    // Colour emoji drawn by the system's text stack (android.graphics.Paint).
    [[nodiscard]] bool hasGlyph(char32_t codepoint);
    [[nodiscard]] std::optional<EmojiGlyph> drawGlyph(char32_t codepoint, float textSizePx);

    [[nodiscard]] innertube::Locale locale() const;
    [[nodiscard]] float density() const; // Pixels per density-independent pixel.
    // The text size chosen in Android's settings: 1 normal, up to 2.
    [[nodiscard]] float fontScale();
    [[nodiscard]] std::filesystem::path dataDirectory() const;
    [[nodiscard]] static std::string_view trustedCertificates();

private:
    [[nodiscard]] jobject window();
    [[nodiscard]] jobject decorView();
    [[nodiscard]] jint systemBarTypes();
    void setOrientation(jint orientation);
    void watchUpright(bool enabled);
    static int onSensorEvents(int fd, int events, void* self);
    [[nodiscard]] jobject paint();    // Global reference, created on first use.
    [[nodiscard]] jobject vibrator(); // Global reference, created on first use.
    [[nodiscard]] jstring codepointString(char32_t codepoint);

    android_app* m_app;
    JNIEnv* m_env = nullptr;
    bool m_keyboardVisible = false;
    bool m_fullscreen = false;
    const ASensor* m_accelerometer = nullptr;
    ASensorEventQueue* m_sensorQueue = nullptr; // Live while portrait is locked after fullscreen.
    jobject m_paint = nullptr;
    jobject m_vibrator = nullptr;
};

} // namespace sloptube::app
