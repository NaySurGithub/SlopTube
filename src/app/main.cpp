// Entry point: owns the Android event loop and forwards lifecycle and input to App. Frames are
// rendered only when something changed (input, a finished task, a new video frame) or while an
// animation runs, so an idle screen costs no CPU or GPU time.

#include "app/App.hpp"
#include "app/util/Guard.hpp"

#include <android/looper.h>
#include <android_native_app_glue.h>

#include <chrono>

namespace {

using sloptube::app::App;
using Clock = std::chrono::steady_clock;

// System bar insets reach the window some time after the resize or configuration change that
// moves them (rotation, entering or leaving fullscreen), so they are re-read for a while.
constexpr auto kInsetsSettleTime = std::chrono::seconds(1);
constexpr int kInsetsPollMs = 100;

struct Host {
    App& app;
    Clock::time_point insetsSettleUntil{};
};

void handleCommandUnguarded(android_app* native, std::int32_t command);

void handleCommand(android_app* native, std::int32_t command) {
    sloptube::app::guarded("lifecycle", [&] { handleCommandUnguarded(native, command); });
}

void handleCommandUnguarded(android_app* native, std::int32_t command) {
    auto& host = *static_cast<Host*>(native->userData);
    App* app = &host.app;
    switch (command) {
    case APP_CMD_INIT_WINDOW:
        app->onWindowCreated(native->window);
        break;
    case APP_CMD_TERM_WINDOW:
        app->onWindowDestroyed();
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONTENT_RECT_CHANGED:
    case APP_CMD_CONFIG_CHANGED:
        app->onInsetsChanged();
        host.insetsSettleUntil = Clock::now() + kInsetsSettleTime;
        break;
    case APP_CMD_PAUSE:
        app->onPause();
        break;
    case APP_CMD_STOP:
        app->onStop();
        break;
    default:
        break;
    }
}

std::int32_t handleInput(android_app* native, AInputEvent* event) {
    std::int32_t handled = 0;
    sloptube::app::guarded(
        "input", [&] { handled = static_cast<Host*>(native->userData)->app.onInput(event); });
    return handled;
}

} // namespace

void android_main(android_app* native) {
    App app(native);
    Host host{.app = app};
    native->userData = &host;
    native->onAppCmd = handleCommand;
    native->onInputEvent = handleInput;

    bool animating = true;
    while (native->destroyRequested == 0) {
        // Block until an event or wake-up unless an animation needs the next frame.
        const bool settling = Clock::now() < host.insetsSettleUntil;
        int timeout = animating ? 0 : (settling ? kInsetsPollMs : app.idleWakeMs());
        while (true) {
            android_poll_source* source = nullptr;
            const int result =
                ALooper_pollOnce(timeout, nullptr, nullptr, reinterpret_cast<void**>(&source));
            if (source != nullptr) {
                source->process(native, source);
            }
            if (result == ALOOPER_POLL_TIMEOUT || result == ALOOPER_POLL_ERROR ||
                native->destroyRequested != 0) {
                break;
            }
            timeout = 0; // Drain everything already queued before drawing.
        }
        if (settling) {
            app.onInsetsChanged();
        }
        if (native->destroyRequested == 0) {
            // A frame that fails is dropped, and the next one starts afresh.
            if (!sloptube::app::guarded("frame", [&] { animating = app.frame(); })) {
                app.abandonFrame();
                animating = true;
            }
        }
    }
    native->userData = nullptr;
}
