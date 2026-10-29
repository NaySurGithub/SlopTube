#pragma once

#include "app/media/Log.hpp"

#include <android/log.h>

#include <exception>

namespace sloptube::app {

// Runs `body`, logging and swallowing any exception it throws, so that one failure (a full disk,
// an unexpected response, memory running out) costs that task, not the app. Used at every
// boundary the C++ runtime would otherwise end the process at: threads, posted tasks, the frame,
// JNI entry points. Returns whether `body` completed.
template <typename Body>
bool guarded(const char* where, Body&& body) noexcept {
    try {
        body();
        return true;
    } catch (const std::exception& error) {
        __android_log_print(ANDROID_LOG_ERROR, media::kLogTag, "%s: %s", where, error.what());
    } catch (...) {
        __android_log_print(ANDROID_LOG_ERROR, media::kLogTag, "%s: unknown exception", where);
    }
    return false;
}

} // namespace sloptube::app
