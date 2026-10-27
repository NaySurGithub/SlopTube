#include "app/platform/Display.hpp"

#include <array>

namespace sloptube::app {

Display::Display() {
    m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(m_display, nullptr, nullptr);

    constexpr std::array<EGLint, 13> kConfigAttributes{
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE,
        EGL_WINDOW_BIT,
        EGL_RED_SIZE,
        8,
        EGL_GREEN_SIZE,
        8,
        EGL_BLUE_SIZE,
        8,
        EGL_ALPHA_SIZE,
        0,
        EGL_NONE,
    };
    EGLint count = 0;
    eglChooseConfig(m_display, kConfigAttributes.data(), &m_config, 1, &count);
    if (count == 0) {
        return;
    }

    constexpr std::array<EGLint, 3> kContextAttributes{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, kContextAttributes.data());
    // Surfaceless until a window arrives (EGL_KHR_surfaceless_context, universal on Android).
    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, m_context);
}

Display::~Display() {
    detach();
    if (m_context != EGL_NO_CONTEXT) {
        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(m_display, m_context);
    }
    eglTerminate(m_display);
}

void Display::attach(ANativeWindow* window) {
    detach();
    m_surface = eglCreateWindowSurface(m_display, m_config, window, nullptr);
    eglMakeCurrent(m_display, m_surface, m_surface, m_context);
    eglSwapInterval(m_display, 1);
}

void Display::detach() {
    if (m_surface == EGL_NO_SURFACE) {
        return;
    }
    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, m_context);
    eglDestroySurface(m_display, m_surface);
    m_surface = EGL_NO_SURFACE;
}

void Display::present() {
    eglSwapBuffers(m_display, m_surface);
}

} // namespace sloptube::app
