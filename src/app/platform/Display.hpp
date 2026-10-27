#pragma once

#include <EGL/egl.h>

struct ANativeWindow;

namespace sloptube::app {

// EGL display and OpenGL ES 3 context. The context outlives window surfaces so GPU resources
// survive the app being backgrounded.
class Display {
public:
    Display();
    ~Display();

    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    [[nodiscard]] bool valid() const { return m_context != EGL_NO_CONTEXT; }

    [[nodiscard]] bool hasSurface() const { return m_surface != EGL_NO_SURFACE; }

    [[nodiscard]] EGLDisplay eglDisplay() const { return m_display; }

    void attach(ANativeWindow* window);
    void detach();
    void present();

private:
    EGLDisplay m_display = EGL_NO_DISPLAY;
    EGLConfig m_config = nullptr;
    EGLContext m_context = EGL_NO_CONTEXT;
    EGLSurface m_surface = EGL_NO_SURFACE;
};

} // namespace sloptube::app
