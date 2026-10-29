#include "app/platform/Platform.hpp"

#include "app/platform/Bridge.hpp"
#include "core/BuildInfo.hpp"

#include <android/bitmap.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/looper.h>
#include <android/sensor.h>
#include <android_native_app_glue.h>

#include <array>
#include <cmath>
#include <filesystem>

namespace sloptube::app {
namespace {

constexpr float kBaselineDpi = 160.0f;

// ActivityInfo.SCREEN_ORIENTATION_*, WindowInsetsController.BEHAVIOR_*,
// HapticFeedbackConstants and VibrationEffect values.
constexpr jint kOrientationUnspecified = -1;
constexpr jint kOrientationPortrait = 1;
constexpr jint kOrientationSensorLandscape = 6;
constexpr jint kBehaviorShowTransientBarsBySwipe = 2;
constexpr jint kHapticLongPress = 0;
constexpr jint kHapticContextClick = 6;
constexpr jint kEffectClick = 0;
constexpr jint kEffectTick = 2;
constexpr jlong kSeekPulseMs = 25;
constexpr jint kFullAmplitude = 255;

// Gravity (m/s²) along the device's long axis above which it counts as held upright, while the
// cross axis stays below the second bound.
constexpr float kUprightGravity = 6.5f;
constexpr float kUprightMaxSideways = 4.0f;
constexpr std::int32_t kSensorPeriodUs = 200'000;
constexpr int kSensorLooperId = LOOPER_ID_USER;

// Android 14 moved the updatable CA store into the Conscrypt APEX.
constexpr std::array<std::string_view, 2> kCertificateDirectories{
    "/apex/com.android.conscrypt/cacerts",
    "/system/etc/security/cacerts",
};

// Deletes JNI local references when leaving scope, keeping long-running callers leak free.
class LocalFrame {
public:
    explicit LocalFrame(JNIEnv* env, jint capacity = 16)
        : m_env(env) {
        m_env->PushLocalFrame(capacity);
    }

    ~LocalFrame() { m_env->PopLocalFrame(nullptr); }

    LocalFrame(const LocalFrame&) = delete;
    LocalFrame& operator=(const LocalFrame&) = delete;

private:
    JNIEnv* m_env;
};

jmethodID method(JNIEnv* env, jobject object, const char* name, const char* signature) {
    return env->GetMethodID(env->GetObjectClass(object), name, signature);
}

} // namespace

Platform::Platform(android_app* app)
    : m_app(app) {
    m_app->activity->vm->AttachCurrentThread(&m_env, nullptr);
}

Platform::~Platform() {
    watchUpright(false);
    for (jobject global : {m_paint, m_vibrator}) {
        if (global != nullptr) {
            m_env->DeleteGlobalRef(global);
        }
    }
    m_app->activity->vm->DetachCurrentThread();
}

jobject Platform::window() {
    jobject activity = m_app->activity->clazz;
    return m_env->CallObjectMethod(activity,
                                   method(m_env, activity, "getWindow", "()Landroid/view/Window;"));
}

jobject Platform::decorView() {
    jobject view = window();
    return m_env->CallObjectMethod(view,
                                   method(m_env, view, "getDecorView", "()Landroid/view/View;"));
}

jint Platform::systemBarTypes() {
    jclass type = m_env->FindClass("android/view/WindowInsets$Type");
    return m_env->CallStaticIntMethod(type, m_env->GetStaticMethodID(type, "systemBars", "()I"));
}

void Platform::setKeyboardVisible(bool visible) {
    if (visible == m_keyboardVisible) {
        return;
    }
    m_keyboardVisible = visible;

    const LocalFrame frame(m_env);
    jobject activity = m_app->activity->clazz;
    jstring service = m_env->NewStringUTF("input_method");
    jobject manager = m_env->CallObjectMethod(
        activity,
        method(m_env, activity, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;"),
        service);
    jobject view = decorView();
    if (visible) {
        m_env->CallBooleanMethod(
            manager, method(m_env, manager, "showSoftInput", "(Landroid/view/View;I)Z"), view, 0);
    } else {
        jobject token = m_env->CallObjectMethod(
            view, method(m_env, view, "getWindowToken", "()Landroid/os/IBinder;"));
        m_env->CallBooleanMethod(
            manager, method(m_env, manager, "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z"),
            token, 0);
    }
}

char32_t Platform::unicodeChar(const AInputEvent* keyEvent) {
    const LocalFrame frame(m_env);
    jclass type = m_env->FindClass("android/view/KeyEvent");
    jmethodID constructor = m_env->GetMethodID(type, "<init>", "(JJIIII)V");
    jobject event =
        m_env->NewObject(type, constructor, static_cast<jlong>(AKeyEvent_getDownTime(keyEvent)),
                         static_cast<jlong>(AKeyEvent_getEventTime(keyEvent)),
                         AKeyEvent_getAction(keyEvent), AKeyEvent_getKeyCode(keyEvent),
                         AKeyEvent_getRepeatCount(keyEvent), AKeyEvent_getMetaState(keyEvent));
    const jint character =
        m_env->CallIntMethod(event, m_env->GetMethodID(type, "getUnicodeChar", "(I)I"),
                             AKeyEvent_getMetaState(keyEvent));
    return static_cast<char32_t>(character);
}

Insets Platform::systemBarInsets() {
    const LocalFrame frame(m_env);
    jobject view = decorView();
    jobject windowInsets = m_env->CallObjectMethod(
        view, method(m_env, view, "getRootWindowInsets", "()Landroid/view/WindowInsets;"));
    if (windowInsets == nullptr) {
        return {};
    }
    jclass typeClass = m_env->FindClass("android/view/WindowInsets$Type");
    const jint cutout = m_env->CallStaticIntMethod(
        typeClass, m_env->GetStaticMethodID(typeClass, "displayCutout", "()I"));
    jobject insets = m_env->CallObjectMethod(
        windowInsets, method(m_env, windowInsets, "getInsets", "(I)Landroid/graphics/Insets;"),
        systemBarTypes() | cutout);
    jclass insetsClass = m_env->GetObjectClass(insets);
    const auto side = [&](const char* name) {
        return static_cast<float>(
            m_env->GetIntField(insets, m_env->GetFieldID(insetsClass, name, "I")));
    };
    return Insets{
        .top = side("top"),
        .bottom = side("bottom"),
        .left = side("left"),
        .right = side("right"),
    };
}

void Platform::setFullscreen(bool fullscreen) {
    if (fullscreen == m_fullscreen) {
        return;
    }
    m_fullscreen = fullscreen;
    // Portrait right away when leaving, as the official app does; otherwise a phone lying flat
    // would keep the landscape orientation.
    setOrientation(fullscreen ? kOrientationSensorLandscape : kOrientationPortrait);
    watchUpright(!fullscreen);

    const LocalFrame frame(m_env);
    jobject view = window();
    jobject controller =
        m_env->CallObjectMethod(view, method(m_env, view, "getInsetsController",
                                             "()Landroid/view/WindowInsetsController;"));
    if (controller == nullptr) {
        return;
    }
    if (fullscreen) {
        m_env->CallVoidMethod(controller,
                              method(m_env, controller, "setSystemBarsBehavior", "(I)V"),
                              kBehaviorShowTransientBarsBySwipe);
    }
    m_env->CallVoidMethod(controller,
                          method(m_env, controller, fullscreen ? "hide" : "show", "(I)V"),
                          systemBarTypes());
    if (m_env->ExceptionCheck()) {
        m_env->ExceptionDescribe();
        m_env->ExceptionClear();
    }
}

void Platform::haptic(Haptic haptic) {
    const LocalFrame frame(m_env);
    if (haptic == Haptic::LongPress || haptic == Haptic::ContextClick) {
        jobject view = decorView();
        m_env->CallBooleanMethod(view, method(m_env, view, "performHapticFeedback", "(I)Z"),
                                 haptic == Haptic::LongPress ? kHapticLongPress
                                                             : kHapticContextClick);
        return;
    }
    jobject device = vibrator();
    if (device == nullptr) {
        return;
    }
    jclass effectClass = m_env->FindClass("android/os/VibrationEffect");
    jobject effect = haptic == Haptic::Seek
                         ? m_env->CallStaticObjectMethod(
                               effectClass,
                               m_env->GetStaticMethodID(effectClass, "createOneShot",
                                                        "(JI)Landroid/os/VibrationEffect;"),
                               kSeekPulseMs, kFullAmplitude)
                         : m_env->CallStaticObjectMethod(
                               effectClass,
                               m_env->GetStaticMethodID(effectClass, "createPredefined",
                                                        "(I)Landroid/os/VibrationEffect;"),
                               haptic == Haptic::Click ? kEffectClick : kEffectTick);
    m_env->CallVoidMethod(
        device, method(m_env, device, "vibrate", "(Landroid/os/VibrationEffect;)V"), effect);
    if (m_env->ExceptionCheck()) {
        m_env->ExceptionClear(); // Vibration is a nicety; a refusal changes nothing else.
    }
}

void Platform::setOrientation(jint orientation) {
    const LocalFrame frame(m_env);
    jobject activity = m_app->activity->clazz;
    m_env->CallVoidMethod(activity, method(m_env, activity, "setRequestedOrientation", "(I)V"),
                          orientation);
}

void Platform::watchUpright(bool enabled) {
    ASensorManager* manager = ASensorManager_getInstanceForPackage(build::kAppId.data());
    // A queue whose callback already fired is unregistered from the looper; replace it.
    if (m_sensorQueue != nullptr) {
        ASensorEventQueue_disableSensor(m_sensorQueue, m_accelerometer);
        ASensorManager_destroyEventQueue(manager, m_sensorQueue);
        m_sensorQueue = nullptr;
    }
    if (!enabled) {
        return;
    }
    m_accelerometer = ASensorManager_getDefaultSensor(manager, ASENSOR_TYPE_ACCELEROMETER);
    if (m_accelerometer == nullptr) {
        setOrientation(kOrientationUnspecified); // Cannot tell: release the lock at once.
        return;
    }
    m_sensorQueue = ASensorManager_createEventQueue(manager, m_app->looper, kSensorLooperId,
                                                    &Platform::onSensorEvents, this);
    ASensorEventQueue_enableSensor(m_sensorQueue, m_accelerometer);
    ASensorEventQueue_setEventRate(m_sensorQueue, m_accelerometer, kSensorPeriodUs);
}

int Platform::onSensorEvents(int, int, void* self) {
    auto& platform = *static_cast<Platform*>(self);
    ASensorEvent event{};
    bool upright = false;
    while (ASensorEventQueue_getEvents(platform.m_sensorQueue, &event, 1) > 0) {
        upright = event.acceleration.y > kUprightGravity &&
                  std::abs(event.acceleration.x) < kUprightMaxSideways;
    }
    if (!upright) {
        return 1; // Keep receiving events.
    }
    platform.setOrientation(kOrientationUnspecified);
    ASensorEventQueue_disableSensor(platform.m_sensorQueue, platform.m_accelerometer);
    return 0; // Unregisters the callback; the queue is freed by the next watchUpright().
}

void Platform::openUrl(std::string_view url) {
    const LocalFrame frame(m_env);
    jclass uriClass = m_env->FindClass("android/net/Uri");
    jobject uri = m_env->CallStaticObjectMethod(
        uriClass,
        m_env->GetStaticMethodID(uriClass, "parse", "(Ljava/lang/String;)Landroid/net/Uri;"),
        m_env->NewStringUTF(std::string(url).c_str()));

    jclass intentClass = m_env->FindClass("android/content/Intent");
    jobject intent = m_env->NewObject(
        intentClass,
        m_env->GetMethodID(intentClass, "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V"),
        m_env->NewStringUTF("android.intent.action.VIEW"), uri);

    jobject activity = m_app->activity->clazz;
    m_env->CallVoidMethod(
        activity, method(m_env, activity, "startActivity", "(Landroid/content/Intent;)V"), intent);
    if (m_env->ExceptionCheck()) {
        m_env->ExceptionClear(); // No browser installed.
    }
}

void Platform::share(std::string_view subject, std::string_view text) {
    const LocalFrame frame(m_env);
    jclass intentClass = m_env->FindClass("android/content/Intent");
    jobject intent = m_env->NewObject(
        intentClass, m_env->GetMethodID(intentClass, "<init>", "(Ljava/lang/String;)V"),
        m_env->NewStringUTF("android.intent.action.SEND"));
    m_env->CallObjectMethod(
        intent,
        m_env->GetMethodID(intentClass, "setType", "(Ljava/lang/String;)Landroid/content/Intent;"),
        m_env->NewStringUTF("text/plain"));
    const jmethodID putExtra = m_env->GetMethodID(
        intentClass, "putExtra", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;");
    m_env->CallObjectMethod(intent, putExtra, m_env->NewStringUTF("android.intent.extra.SUBJECT"),
                            javaString(m_env, subject));
    m_env->CallObjectMethod(intent, putExtra, m_env->NewStringUTF("android.intent.extra.TEXT"),
                            javaString(m_env, text));
    jobject chooser = m_env->CallStaticObjectMethod(
        intentClass,
        m_env->GetStaticMethodID(
            intentClass, "createChooser",
            "(Landroid/content/Intent;Ljava/lang/CharSequence;)Landroid/content/Intent;"),
        intent, javaString(m_env, subject));

    jobject activity = m_app->activity->clazz;
    m_env->CallVoidMethod(
        activity, method(m_env, activity, "startActivity", "(Landroid/content/Intent;)V"), chooser);
    if (m_env->ExceptionCheck()) {
        m_env->ExceptionClear();
    }
}

void Platform::updatePlayback(const Playback& playback) {
    const LocalFrame frame(m_env);
    jobject activity = m_app->activity->clazz;
    m_env->CallVoidMethod(
        activity,
        method(m_env, activity, "updatePlayback",
               "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;JJZZFZZ)V"),
        javaString(m_env, playback.videoId), javaString(m_env, playback.title),
        javaString(m_env, playback.channel), static_cast<jlong>(playback.durationMs),
        static_cast<jlong>(playback.positionMs), static_cast<jboolean>(playback.playing),
        static_cast<jboolean>(playback.buffering), static_cast<jfloat>(playback.speed),
        static_cast<jboolean>(playback.hasPrevious), static_cast<jboolean>(playback.hasNext));
}

void Platform::stopPlayback() {
    const LocalFrame frame(m_env); // The app thread never returns to Java to free them.
    jobject activity = m_app->activity->clazz;
    m_env->CallVoidMethod(activity, method(m_env, activity, "stopPlayback", "()V"));
}

void Platform::setPictureInPicture(bool allowed, int width, int height) {
    const LocalFrame frame(m_env); // The app thread never returns to Java to free them.
    jobject activity = m_app->activity->clazz;
    m_env->CallVoidMethod(activity, method(m_env, activity, "setPictureInPicture", "(ZII)V"),
                          static_cast<jboolean>(allowed), static_cast<jint>(width),
                          static_cast<jint>(height));
}

void Platform::setUploadNotifications(bool enabled) {
    const LocalFrame frame(m_env); // The app thread never returns to Java to free them.
    jobject activity = m_app->activity->clazz;
    m_env->CallVoidMethod(activity, method(m_env, activity, "setUploadNotifications", "(Z)V"),
                          static_cast<jboolean>(enabled));
}

innertube::Locale Platform::locale() const {
    std::array<char, 2> language{};
    std::array<char, 2> country{};
    AConfiguration_getLanguage(m_app->config, language.data());
    AConfiguration_getCountry(m_app->config, country.data());
    innertube::Locale locale;
    if (language[0] != 0) {
        locale.language.assign(language.data(), language.size());
    }
    if (country[0] != 0) {
        locale.region.assign(country.data(), country.size());
    }
    return locale;
}

float Platform::density() const {
    return static_cast<float>(AConfiguration_getDensity(m_app->config)) / kBaselineDpi;
}

float Platform::fontScale() {
    const LocalFrame frame(m_env);
    jobject activity = m_app->activity->clazz;
    jobject resources = m_env->CallObjectMethod(
        activity, method(m_env, activity, "getResources", "()Landroid/content/res/Resources;"));
    jobject configuration =
        m_env->CallObjectMethod(resources, method(m_env, resources, "getConfiguration",
                                                  "()Landroid/content/res/Configuration;"));
    if (m_env->ExceptionCheck() || configuration == nullptr) {
        m_env->ExceptionClear();
        return 1;
    }
    const jfieldID field =
        m_env->GetFieldID(m_env->GetObjectClass(configuration), "fontScale", "F");
    return field != nullptr ? m_env->GetFloatField(configuration, field) : 1;
}

std::filesystem::path Platform::dataDirectory() const {
    return m_app->activity->internalDataPath;
}

std::string_view Platform::trustedCertificates() {
    for (const auto directory : kCertificateDirectories) {
        std::error_code error;
        if (std::filesystem::is_directory(directory, error)) {
            return directory;
        }
    }
    return {};
}

jobject Platform::paint() {
    if (m_paint == nullptr) {
        constexpr jint kAntiAlias = 1; // Paint.ANTI_ALIAS_FLAG
        LocalFrame frame(m_env);
        jclass paintClass = m_env->FindClass("android/graphics/Paint");
        jobject local = m_env->NewObject(
            paintClass, m_env->GetMethodID(paintClass, "<init>", "(I)V"), kAntiAlias);
        m_paint = m_env->NewGlobalRef(local);
    }
    return m_paint;
}

jobject Platform::vibrator() {
    if (m_vibrator == nullptr) {
        const LocalFrame frame(m_env);
        jobject activity = m_app->activity->clazz;
        jstring service = m_env->NewStringUTF("vibrator");
        jobject local = m_env->CallObjectMethod(
            activity,
            method(m_env, activity, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;"),
            service);
        if (local != nullptr) {
            m_vibrator = m_env->NewGlobalRef(local);
        }
    }
    return m_vibrator;
}

jstring Platform::codepointString(char32_t codepoint) {
    std::array<jchar, 2> units{};
    jsize length = 1;
    if (codepoint > 0xFFFF) {
        const char32_t offset = codepoint - 0x10000;
        units = {static_cast<jchar>(0xD800 + (offset >> 10)),
                 static_cast<jchar>(0xDC00 + (offset & 0x3FF))};
        length = 2;
    } else {
        units[0] = static_cast<jchar>(codepoint);
    }
    return m_env->NewString(units.data(), length);
}

bool Platform::hasGlyph(char32_t codepoint) {
    LocalFrame frame(m_env);
    jobject p = paint();
    return m_env->CallBooleanMethod(p, method(m_env, p, "hasGlyph", "(Ljava/lang/String;)Z"),
                                    codepointString(codepoint)) == JNI_TRUE;
}

std::optional<EmojiGlyph> Platform::drawGlyph(char32_t codepoint, float textSizePx) {
    LocalFrame frame(m_env, 32);
    jobject p = paint();
    jstring text = codepointString(codepoint);
    m_env->CallVoidMethod(p, method(m_env, p, "setTextSize", "(F)V"), textSizePx);
    const float advance =
        m_env->CallFloatMethod(p, method(m_env, p, "measureText", "(Ljava/lang/String;)F"), text);
    jobject metrics = m_env->CallObjectMethod(
        p, method(m_env, p, "getFontMetrics", "()Landroid/graphics/Paint$FontMetrics;"));
    jclass metricsClass = m_env->GetObjectClass(metrics);
    const float top = m_env->GetFloatField(metrics, m_env->GetFieldID(metricsClass, "top", "F"));
    const float bottom =
        m_env->GetFloatField(metrics, m_env->GetFieldID(metricsClass, "bottom", "F"));

    EmojiGlyph glyph{.width = static_cast<int>(std::ceil(advance)),
                     .height = static_cast<int>(std::ceil(bottom - top)),
                     .baseline = -top,
                     .advance = advance};
    if (glyph.width <= 0 || glyph.height <= 0) {
        return std::nullopt;
    }

    jclass configClass = m_env->FindClass("android/graphics/Bitmap$Config");
    jobject argb = m_env->GetStaticObjectField(
        configClass,
        m_env->GetStaticFieldID(configClass, "ARGB_8888", "Landroid/graphics/Bitmap$Config;"));
    jclass bitmapClass = m_env->FindClass("android/graphics/Bitmap");
    jobject bitmap = m_env->CallStaticObjectMethod(
        bitmapClass,
        m_env->GetStaticMethodID(bitmapClass, "createBitmap",
                                 "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;"),
        glyph.width, glyph.height, argb);
    jclass canvasClass = m_env->FindClass("android/graphics/Canvas");
    jobject canvas = m_env->NewObject(
        canvasClass, m_env->GetMethodID(canvasClass, "<init>", "(Landroid/graphics/Bitmap;)V"),
        bitmap);
    m_env->CallVoidMethod(canvas,
                          m_env->GetMethodID(canvasClass, "drawText",
                                             "(Ljava/lang/String;FFLandroid/graphics/Paint;)V"),
                          text, 0.0f, glyph.baseline, p);
    if (m_env->ExceptionCheck()) {
        m_env->ExceptionClear();
        return std::nullopt;
    }

    // ARGB_8888 is stored as premultiplied R, G, B, A bytes; ImGui blends straight alpha.
    AndroidBitmapInfo info{};
    void* pixels = nullptr;
    if (AndroidBitmap_getInfo(m_env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS ||
        AndroidBitmap_lockPixels(m_env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS) {
        return std::nullopt;
    }
    glyph.pixels.resize(static_cast<std::size_t>(glyph.width) *
                        static_cast<std::size_t>(glyph.height));
    for (std::uint32_t y = 0; y < info.height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(pixels) + y * info.stride;
        for (std::uint32_t x = 0; x < info.width; ++x) {
            const std::uint8_t* px = row + x * 4;
            const std::uint32_t a = px[3];
            const auto straight = [a](std::uint32_t c) {
                return a == 0 ? 0 : std::min(255u, c * 255 / a);
            };
            glyph.pixels[y * info.width + x] =
                straight(px[0]) | (straight(px[1]) << 8) | (straight(px[2]) << 16) | (a << 24);
        }
    }
    AndroidBitmap_unlockPixels(m_env, bitmap);
    m_env->CallVoidMethod(bitmap, m_env->GetMethodID(bitmapClass, "recycle", "()V"));
    return glyph;
}

} // namespace sloptube::app
