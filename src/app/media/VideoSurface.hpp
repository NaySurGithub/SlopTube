#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <imgui.h>
#include <media/NdkImageReader.h>

#include <cstdint>

namespace sloptube::app::media {

// Samples decoder output buffers directly as an external GL texture (no CPU copy or YUV
// conversion) and draws it inside ImGui's render pass.
class VideoSurface {
public:
    explicit VideoSurface(EGLDisplay display);
    ~VideoSurface();

    VideoSurface(const VideoSurface&) = delete;
    VideoSurface& operator=(const VideoSurface&) = delete;

    // Latches the newest frame from `reader`; returns whether one was available.
    bool update(AImageReader* reader);
    // Releases the held frame; required before the reader is destroyed.
    void release();

    [[nodiscard]] bool hasFrame() const { return m_image != nullptr; }

    // Queues drawing of the current frame, letterboxed into [min, max] and scaled by `zoom`
    // about the box's centre; whatever then falls outside the box is cropped.
    void draw(ImDrawList* list, ImVec2 min, ImVec2 max, float zoom = 1);

    // Zoom at which the current frame covers all of [min, max] (1 when it already does).
    [[nodiscard]] float fillZoom(ImVec2 min, ImVec2 max) const;

private:
    static void renderCallback(const ImDrawList* list, const ImDrawCmd* command);
    void render() const;

    EGLDisplay m_display;
    GLuint m_program = 0;
    GLuint m_texture = 0;
    GLint m_rectLocation = -1;
    GLint m_uvLocation = -1;
    AImage* m_image = nullptr;
    EGLImageKHR m_eglImage = EGL_NO_IMAGE_KHR;
    float m_aspect = 16.0f / 9.0f;
    std::int32_t m_frameWidth = 0; // Visible size of the latest frame.
    std::int32_t m_frameHeight = 0;
    ImVec4 m_uv{0, 0, 1, 1};     // Crop of the buffer holding visible pixels.
    ImVec4 m_drawUv{0, 0, 1, 1}; // Part of m_uv drawn, once zoomed and cropped to the box.
    ImVec4 m_rect{0, 0, 0, 0};   // Destination in normalised device coordinates.
};

} // namespace sloptube::app::media
