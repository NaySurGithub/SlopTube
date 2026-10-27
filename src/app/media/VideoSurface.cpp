#include "app/media/VideoSurface.hpp"

#include "app/media/Log.hpp"

#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <android/log.h>

#include <algorithm>
#include <array>

namespace sloptube::app::media {
namespace {

// Full-screen-quad vertices are generated in the shader from gl_VertexID; no buffers needed.
constexpr const char* kVertexShader = R"(#version 300 es
uniform vec4 uRect; // x0, y0, x1, y1 in NDC
uniform vec4 uUv;   // u0, v0, u1, v1
out vec2 vUv;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vUv = mix(uUv.xy, uUv.zw, corner);
    gl_Position = vec4(mix(uRect.xy, uRect.zw, corner), 0.0, 1.0);
})";

constexpr const char* kFragmentShader = R"(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision mediump float;
uniform samplerExternalOES uTexture;
in vec2 vUv;
out vec4 color;
void main() { color = texture(uTexture, vUv); })";

GLuint compile(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    return shader;
}

const auto kGetClientBuffer = reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(
    eglGetProcAddress("eglGetNativeClientBufferANDROID"));
const auto kCreateImage =
    reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
const auto kDestroyImage =
    reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
const auto kTargetTexture = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
    eglGetProcAddress("glEGLImageTargetTexture2DOES"));

} // namespace

VideoSurface::VideoSurface(EGLDisplay display)
    : m_display(display) {
    const GLuint vertex = compile(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, kFragmentShader);
    m_program = glCreateProgram();
    glAttachShader(m_program, vertex);
    glAttachShader(m_program, fragment);
    glLinkProgram(m_program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    m_rectLocation = glGetUniformLocation(m_program, "uRect");
    m_uvLocation = glGetUniformLocation(m_program, "uUv");

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, m_texture);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

VideoSurface::~VideoSurface() {
    release();
    glDeleteTextures(1, &m_texture);
    glDeleteProgram(m_program);
}

bool VideoSurface::update(AImageReader* reader) {
    if (reader == nullptr) {
        return false;
    }
    AImage* image = nullptr;
    if (AImageReader_acquireLatestImage(reader, &image) != AMEDIA_OK || image == nullptr) {
        return false;
    }
    AHardwareBuffer* buffer = nullptr;
    AImage_getHardwareBuffer(image, &buffer);
    constexpr std::array<EGLint, 3> kAttributes{EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
    EGLImageKHR eglImage = kCreateImage(m_display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
                                        kGetClientBuffer(buffer), kAttributes.data());
    if (eglImage == EGL_NO_IMAGE_KHR) {
        AImage_delete(image);
        return false;
    }
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, m_texture);
    kTargetTexture(GL_TEXTURE_EXTERNAL_OES, eglImage);

    // The previous frame can go only once the texture points at the new one.
    release();
    m_image = image;
    m_eglImage = eglImage;

    AHardwareBuffer_Desc description{};
    AHardwareBuffer_describe(buffer, &description);
    AImageCropRect crop{};
    AImage_getCropRect(image, &crop);
    const auto width = static_cast<float>(description.width);
    const auto height = static_cast<float>(description.height);
    m_uv = {static_cast<float>(crop.left) / width, static_cast<float>(crop.top) / height,
            static_cast<float>(crop.right) / width, static_cast<float>(crop.bottom) / height};
    if (crop.right - crop.left != m_frameWidth || crop.bottom - crop.top != m_frameHeight) {
        m_frameWidth = crop.right - crop.left;
        m_frameHeight = crop.bottom - crop.top;
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "presenting %dx%d frames", m_frameWidth,
                            m_frameHeight);
    }
    const auto visibleHeight = static_cast<float>(crop.bottom - crop.top);
    if (visibleHeight > 0) {
        m_aspect = static_cast<float>(crop.right - crop.left) / visibleHeight;
    }
    return true;
}

void VideoSurface::release() {
    if (m_eglImage != EGL_NO_IMAGE_KHR) {
        kDestroyImage(m_display, m_eglImage);
        m_eglImage = EGL_NO_IMAGE_KHR;
    }
    if (m_image != nullptr) {
        AImage_delete(m_image);
        m_image = nullptr;
    }
}

void VideoSurface::draw(ImDrawList* list, ImVec2 min, ImVec2 max, float zoom) {
    if (m_image == nullptr) {
        return;
    }
    // Letterbox: fit the frame's aspect ratio inside the box, then zoom.
    float width = max.x - min.x;
    float height = width / m_aspect;
    if (height > max.y - min.y) {
        height = max.y - min.y;
        width = height * m_aspect;
    }
    width *= zoom;
    height *= zoom;
    const ImVec2 origin{min.x + (max.x - min.x - width) * 0.5f,
                        min.y + (max.y - min.y - height) * 0.5f};

    // Crop to the box, taking the same fraction off the texture coordinates.
    const float left = std::max(origin.x, min.x);
    const float top = std::max(origin.y, min.y);
    const float right = std::min(origin.x + width, max.x);
    const float bottom = std::min(origin.y + height, max.y);
    if (right <= left || bottom <= top) {
        return;
    }
    const auto u = [&](float x) { return m_uv.x + (m_uv.z - m_uv.x) * (x - origin.x) / width; };
    const auto v = [&](float y) { return m_uv.y + (m_uv.w - m_uv.y) * (y - origin.y) / height; };
    m_drawUv = {u(left), v(top), u(right), v(bottom)};

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const auto ndcX = [&](float x) { return x / display.x * 2.0f - 1.0f; };
    const auto ndcY = [&](float y) { return 1.0f - y / display.y * 2.0f; };
    // Bottom-left corner first so the quad's v axis matches the buffer's top-down rows.
    m_rect = {ndcX(left), ndcY(bottom), ndcX(right), ndcY(top)};

    list->AddCallback(&VideoSurface::renderCallback, this);
    list->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
}

float VideoSurface::fillZoom(ImVec2 min, ImVec2 max) const {
    const float box = (max.x - min.x) / (max.y - min.y);
    return box > m_aspect ? box / m_aspect : m_aspect / box;
}

void VideoSurface::renderCallback(const ImDrawList*, const ImDrawCmd* command) {
    static_cast<const VideoSurface*>(command->UserCallbackData)->render();
}

void VideoSurface::render() const {
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glUseProgram(m_program);
    glUniform4f(m_rectLocation, m_rect.x, m_rect.y, m_rect.z, m_rect.w);
    // Flip v: the quad's first row is the bottom of the frame.
    glUniform4f(m_uvLocation, m_drawUv.x, m_drawUv.w, m_drawUv.z, m_drawUv.y);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, m_texture);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

} // namespace sloptube::app::media
