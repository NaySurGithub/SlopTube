#include "app/platform/Bridge.hpp"

#include "app/media/Log.hpp"
#include "app/notifications/Uploads.hpp"
#include "app/util/Guard.hpp"
#include "core/BuildInfo.hpp"

#include <android/log.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <vector>

namespace sloptube::app {
namespace {

std::mutex g_mutex;
std::function<void(BridgeEvent)> g_handler; // Under g_mutex.
std::vector<BridgeEvent> g_pending;         // Under g_mutex: sent before a handler was set.

void dispatchLocked(BridgeEvent event);

// Exceptions must not cross back into Java, which would end the process.
void dispatch(BridgeEvent event) {
    guarded("bridge event", [&] { dispatchLocked(std::move(event)); });
}

void dispatchLocked(BridgeEvent event) {
    // Under the lock, so setBridgeHandler(nullptr) waits for a delivery in flight: the handler
    // only posts to the main thread, and never comes back here.
    const std::scoped_lock lock(g_mutex);
    if (g_handler) {
        g_handler(std::move(event));
    } else if (event.kind == BridgeEvent::Kind::OpenVideo) {
        // A notification tap that launched the app; anything else is stale by the time the
        // app exists.
        g_pending.push_back(std::move(event));
    }
}

void JNICALL openVideo(JNIEnv* env, jclass, jstring videoId) {
    dispatch({.kind = BridgeEvent::Kind::OpenVideo, .videoId = nativeString(env, videoId)});
}

void JNICALL pictureInPicture(JNIEnv*, jclass, jboolean active) {
    dispatch({.kind = BridgeEvent::Kind::PictureInPicture, .active = active == JNI_TRUE});
}

void JNICALL updateFailed(JNIEnv* env, jclass, jstring reason) {
    dispatch({.kind = BridgeEvent::Kind::UpdateFailed, .reason = nativeString(env, reason)});
}

void JNICALL voiceSearch(JNIEnv* env, jclass, jstring query) {
    dispatch({.kind = BridgeEvent::Kind::VoiceSearch, .query = nativeString(env, query)});
}

void JNICALL pictureInPictureDismissed(JNIEnv*, jclass) {
    dispatch({.kind = BridgeEvent::Kind::PictureInPictureDismissed});
}

void JNICALL mediaCommand(JNIEnv*, jclass, jint command, jlong positionMs) {
    if (command < 0 || command > static_cast<jint>(MediaCommand::Stop)) {
        return;
    }
    dispatch({.kind = BridgeEvent::Kind::Media,
              .command = static_cast<MediaCommand>(command),
              .positionMs = positionMs});
}

// Runs on the job's own thread, possibly while the app is not running.
jobjectArray JNICALL newUploadsOf(JNIEnv* env, jclass, jstring dataDirectory, jstring language,
                                  jstring region) {
    innertube::Locale locale;
    if (std::string code = nativeString(env, language); !code.empty()) {
        locale.language = std::move(code);
    }
    if (std::string code = nativeString(env, region); !code.empty()) {
        locale.region = std::move(code);
    }
    std::vector<Upload> uploads;
    guarded("upload check",
            [&] { uploads = newUploads(nativeString(env, dataDirectory), std::move(locale)); });
    constexpr jsize kFields = 3; // UploadsJob.FIELDS.
    jobjectArray result = env->NewObjectArray(static_cast<jsize>(uploads.size()) * kFields,
                                              env->FindClass("java/lang/String"), nullptr);
    jsize index = 0;
    for (const Upload& upload : uploads) {
        for (const std::string* field : {&upload.videoId, &upload.title, &upload.channel}) {
            jstring value = javaString(env, *field);
            env->SetObjectArrayElement(result, index++, value);
            env->DeleteLocalRef(value);
        }
    }
    return result;
}

// "studio.kanye.sloptube" + "SlopTubeActivity" -> "studio/kanye/sloptube/SlopTubeActivity".
std::string className(std::string_view name) {
    std::string result(build::kAppId);
    std::ranges::replace(result, '.', '/');
    result += '/';
    result += name;
    return result;
}

bool registerNatives(JNIEnv* env, std::string_view name, std::span<const JNINativeMethod> methods) {
    jclass type = env->FindClass(className(name).c_str());
    if (type == nullptr ||
        env->RegisterNatives(type, methods.data(), static_cast<jint>(methods.size())) != JNI_OK) {
        env->ExceptionClear();
        __android_log_print(ANDROID_LOG_ERROR, media::kLogTag, "bridge: cannot register %.*s",
                            static_cast<int>(name.size()), name.data());
        return false;
    }
    env->DeleteLocalRef(type);
    return true;
}

} // namespace

void setBridgeHandler(std::function<void(BridgeEvent)> handler) {
    const std::scoped_lock lock(g_mutex);
    g_handler = std::move(handler);
    if (g_handler) {
        for (BridgeEvent& event : g_pending) {
            g_handler(std::move(event));
        }
        g_pending.clear();
    }
}

jstring javaString(JNIEnv* env, std::string_view utf8) {
    std::u16string utf16;
    utf16.reserve(utf8.size());
    for (std::size_t i = 0; i < utf8.size();) {
        const auto lead = static_cast<unsigned char>(utf8[i]);
        const std::size_t length = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        if (i + length > utf8.size()) {
            break; // Truncated sequence.
        }
        char32_t codepoint = length == 1   ? lead
                             : length == 2 ? lead & 0x1fU
                             : length == 3 ? lead & 0x0fU
                                           : lead & 0x07U;
        for (std::size_t k = 1; k < length; ++k) {
            codepoint = (codepoint << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3fU);
        }
        i += length;
        if (codepoint >= 0x10000) {
            codepoint -= 0x10000;
            utf16.push_back(static_cast<char16_t>(0xd800 + (codepoint >> 10)));
            utf16.push_back(static_cast<char16_t>(0xdc00 + (codepoint & 0x3ffU)));
        } else {
            utf16.push_back(static_cast<char16_t>(codepoint));
        }
    }
    return env->NewString(reinterpret_cast<const jchar*>(utf16.data()),
                          static_cast<jsize>(utf16.size()));
}

std::string nativeString(JNIEnv* env, jstring text) {
    std::string utf8;
    if (text == nullptr) {
        return utf8;
    }
    const jsize length = env->GetStringLength(text);
    const jchar* chars = env->GetStringChars(text, nullptr);
    for (jsize i = 0; i < length; ++i) {
        char32_t codepoint = chars[i];
        if (codepoint >= 0xd800 && codepoint < 0xe000) {
            // A surrogate pair, or an unpaired half, which becomes the replacement character.
            const bool paired = codepoint < 0xdc00 && i + 1 < length && chars[i + 1] >= 0xdc00 &&
                                chars[i + 1] < 0xe000;
            codepoint = paired ? 0x10000 + ((codepoint - 0xd800) << 10) + (chars[++i] - 0xdc00U)
                               : U'\uFFFD';
        }
        if (codepoint < 0x80) {
            utf8 += static_cast<char>(codepoint);
        } else if (codepoint < 0x800) {
            utf8 += static_cast<char>(0xc0 | (codepoint >> 6));
            utf8 += static_cast<char>(0x80 | (codepoint & 0x3f));
        } else if (codepoint < 0x10000) {
            utf8 += static_cast<char>(0xe0 | (codepoint >> 12));
            utf8 += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
            utf8 += static_cast<char>(0x80 | (codepoint & 0x3f));
        } else {
            utf8 += static_cast<char>(0xf0 | (codepoint >> 18));
            utf8 += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f));
            utf8 += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
            utf8 += static_cast<char>(0x80 | (codepoint & 0x3f));
        }
    }
    env->ReleaseStringChars(text, chars);
    return utf8;
}

} // namespace sloptube::app

// Called when SlopTubeActivity or UploadsJob loads the library, with their class loader, so the
// bridge classes are found here (FindClass on native threads only sees the system's classes).
extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    using namespace sloptube::app;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    const std::array activity{
        JNINativeMethod{"nativeOpenVideo", "(Ljava/lang/String;)V",
                        reinterpret_cast<void*>(&openVideo)},
        JNINativeMethod{"nativePictureInPicture", "(Z)V",
                        reinterpret_cast<void*>(&pictureInPicture)},
        JNINativeMethod{"nativePictureInPictureDismissed", "()V",
                        reinterpret_cast<void*>(&pictureInPictureDismissed)},
        JNINativeMethod{"nativeUpdateFailed", "(Ljava/lang/String;)V",
                        reinterpret_cast<void*>(&updateFailed)},
        JNINativeMethod{"nativeVoiceSearch", "(Ljava/lang/String;)V",
                        reinterpret_cast<void*>(&voiceSearch)},
    };
    const std::array service{
        JNINativeMethod{"nativeMediaCommand", "(IJ)V", reinterpret_cast<void*>(&mediaCommand)},
    };
    const std::array job{
        JNINativeMethod{
            "nativeNewUploads",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)[Ljava/lang/String;",
            reinterpret_cast<void*>(&newUploadsOf)},
    };
    registerNatives(env, "SlopTubeActivity", activity);
    registerNatives(env, "PlaybackService", service);
    registerNatives(env, "UploadsJob", job);
    return JNI_VERSION_1_6;
}
