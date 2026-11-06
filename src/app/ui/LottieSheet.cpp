#include "app/ui/LottieSheet.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <thorvg.h>

namespace sloptube::app {
namespace {

// ThorVG is built without its own threads; one render at a time keeps its globals safe.
std::mutex g_thorvg;

} // namespace

std::optional<LottieFrames> renderLottie(const std::string& json, int cellPx, int maxFrames) {
    if (json.empty() || cellPx <= 0 || maxFrames <= 0) {
        return std::nullopt;
    }
    const std::scoped_lock lock(g_thorvg);
    static const bool initialised = tvg::Initializer::init(0) == tvg::Result::Success;
    if (!initialised) {
        return std::nullopt;
    }
    const std::unique_ptr<tvg::Animation> animation(tvg::Animation::gen());
    tvg::Picture* picture = animation->picture();
    if (picture->load(json.data(), static_cast<std::uint32_t>(json.size()), "lottie+json", nullptr,
                      true) != tvg::Result::Success) {
        return std::nullopt;
    }
    const auto side = static_cast<float>(cellPx);
    picture->size(side, side);

    LottieFrames frames;
    const float total = animation->totalFrame();
    frames.duration = animation->duration();
    frames.count = std::clamp(static_cast<int>(std::ceil(total)), 1, maxFrames);
    frames.cell = cellPx;
    frames.columns = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(frames.count))));
    const int rows = (frames.count + frames.columns - 1) / frames.columns;
    const auto stride = static_cast<std::size_t>(frames.columns * cellPx);
    frames.pixels.assign(stride * static_cast<std::size_t>(rows * cellPx), 0);

    // One cell-sized canvas, copied into the sheet after each frame.
    std::vector<std::uint32_t> cell(static_cast<std::size_t>(cellPx * cellPx));
    const std::unique_ptr<tvg::SwCanvas> canvas(tvg::SwCanvas::gen());
    const auto px = static_cast<std::uint32_t>(cellPx);
    // Un-premultiplied RGBA bytes, as ImGui's GL backend blends them.
    if (!canvas || canvas->target(cell.data(), px, px, px, tvg::ColorSpace::ABGR8888S) !=
                       tvg::Result::Success) {
        return std::nullopt;
    }
    canvas->add(picture);
    for (int i = 0; i < frames.count; ++i) {
        // The last cell is the animation's last frame, which holds once it has played.
        const float at = frames.count == 1 ? 0
                                           : (total - 1) * static_cast<float>(i) /
                                                 static_cast<float>(frames.count - 1);
        animation->frame(at);
        canvas->update();
        canvas->draw(true);
        canvas->sync();
        const std::size_t x0 = static_cast<std::size_t>((i % frames.columns) * cellPx);
        const std::size_t y0 = static_cast<std::size_t>((i / frames.columns) * cellPx);
        for (std::size_t y = 0; y < static_cast<std::size_t>(cellPx); ++y) {
            std::copy_n(cell.begin() + static_cast<std::ptrdiff_t>(y * px), cellPx,
                        frames.pixels.begin() +
                            static_cast<std::ptrdiff_t>((y0 + y) * stride + x0));
        }
    }
    canvas->remove(); // The picture belongs to the animation.
    return frames;
}

LottieSheet::~LottieSheet() {
    reset();
}

LottieSheet& LottieSheet::operator=(LottieSheet&& other) noexcept {
    if (this != &other) {
        reset();
        m_texture = std::exchange(other.m_texture, 0);
        m_columns = other.m_columns;
        m_rows = other.m_rows;
        m_count = other.m_count;
        m_duration = other.m_duration;
    }
    return *this;
}

void LottieSheet::reset() {
    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
}

void LottieSheet::upload(const LottieFrames& frames) {
    reset();
    m_columns = frames.columns;
    m_count = frames.count;
    m_rows = (frames.count + frames.columns - 1) / frames.columns;
    m_duration = frames.duration;
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_columns * frames.cell, m_rows * frames.cell, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, frames.pixels.data());
}

void LottieSheet::draw(ImDrawList* list, ImVec2 center, float sizePx, float progress) const {
    if (m_texture == 0) {
        return;
    }
    const int index = std::clamp(static_cast<int>(std::round(std::clamp(progress, 0.0f, 1.0f) *
                                                             static_cast<float>(m_count - 1))),
                                 0, m_count - 1);
    const float u = 1.0f / static_cast<float>(m_columns);
    const float v = 1.0f / static_cast<float>(m_rows);
    const ImVec2 uv0{static_cast<float>(index % m_columns) * u,
                     static_cast<float>(index / m_columns) * v};
    const float half = sizePx * 0.5f;
    list->AddImage(ImTextureRef(static_cast<ImTextureID>(m_texture)),
                   {center.x - half, center.y - half}, {center.x + half, center.y + half}, uv0,
                   {uv0.x + u, uv0.y + v});
}

} // namespace sloptube::app
