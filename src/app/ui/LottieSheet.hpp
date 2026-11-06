#pragma once

#include <GLES3/gl3.h>
#include <imgui.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sloptube::app {

// A Lottie animation rasterised ahead, frame by frame, into one sprite sheet, so that playing it
// costs a textured quad per frame. ThorVG's software engine renders on a worker (about 1.5 ms a
// frame at 144 px on a desktop core, several times that on a low-end phone: too much for the UI
// thread at 60 fps), then the sheet is uploaded once.
struct LottieFrames {
    std::vector<std::uint32_t> pixels; // RGBA, straight alpha, `columns` cells per row.
    int cell = 0;                      // Side of a frame, px.
    int columns = 0;
    int count = 0;
    float duration = 0; // s
};

// Renders `json` (a Lottie file) at `cellPx` square, keeping at most `maxFrames` frames evenly
// spread over its length. Runs on any thread; nullopt when the file cannot be read.
[[nodiscard]] std::optional<LottieFrames> renderLottie(const std::string& json, int cellPx,
                                                       int maxFrames);

// The uploaded sheet. Main thread only.
class LottieSheet {
public:
    LottieSheet() = default;
    ~LottieSheet();
    LottieSheet(const LottieSheet&) = delete;
    LottieSheet& operator=(const LottieSheet&) = delete;

    LottieSheet(LottieSheet&& other) noexcept { *this = std::move(other); }

    LottieSheet& operator=(LottieSheet&& other) noexcept;

    void upload(const LottieFrames& frames);
    void reset();

    [[nodiscard]] bool ready() const { return m_texture != 0; }

    [[nodiscard]] float duration() const { return m_duration; }

    // Draws the frame at `progress` (0 to 1, clamped: the last frame holds) in the square
    // centred on `center`, `sizePx` wide.
    void draw(ImDrawList* list, ImVec2 center, float sizePx, float progress) const;

private:
    GLuint m_texture = 0;
    int m_columns = 0;
    int m_rows = 0;
    int m_count = 0;
    float m_duration = 0;
};

} // namespace sloptube::app
