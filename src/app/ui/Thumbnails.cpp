#include "app/ui/Thumbnails.hpp"

#include "app/util/TaskQueue.hpp"
#include "core/http/Client.hpp"

#include <android/imagedecoder.h>

#include <algorithm>
#include <vector>

namespace sloptube::app {
namespace {

// 480x360 frame that every video has; the 16:9 picture sits inside its letterbox.
constexpr std::string_view kThumbnailPrefix = "https://i.ytimg.com/vi/";
constexpr std::string_view kThumbnailSuffix = "/hqdefault.jpg";

// Avatar URLs carry their size as "=s<pixels>" followed by more options or the end.
constexpr std::string_view kAvatarSizeMarker = "=s";

// Requests not drawn for this many frames are abandoned (scrolled past during a fling).
constexpr std::uint64_t kAbandonAfterFrames = 30;

struct Decoded {
    std::vector<std::uint8_t> pixels;
    int width = 0;
    int height = 0;
};

bool decode(const std::string& encoded, int maxWidth, Decoded& out) {
    AImageDecoder* decoder = nullptr;
    if (AImageDecoder_createFromBuffer(encoded.data(), encoded.size(), &decoder) !=
        ANDROID_IMAGE_DECODER_SUCCESS) {
        return false;
    }
    const AImageDecoderHeaderInfo* info = AImageDecoder_getHeaderInfo(decoder);
    const std::int32_t sourceWidth = AImageDecoderHeaderInfo_getWidth(info);
    const std::int32_t sourceHeight = AImageDecoderHeaderInfo_getHeight(info);
    out.width = std::min(sourceWidth, maxWidth);
    out.height = sourceHeight * out.width / std::max(sourceWidth, 1);

    AImageDecoder_setAndroidBitmapFormat(decoder, ANDROID_BITMAP_FORMAT_RGBA_8888);
    AImageDecoder_setTargetSize(decoder, out.width, out.height);
    const std::size_t stride = AImageDecoder_getMinimumStride(decoder);
    out.pixels.resize(stride * static_cast<std::size_t>(out.height));
    const bool ok = AImageDecoder_decodeImage(decoder, out.pixels.data(), stride,
                                              out.pixels.size()) == ANDROID_IMAGE_DECODER_SUCCESS;
    AImageDecoder_delete(decoder);
    return ok && stride == static_cast<std::size_t>(out.width) * 4;
}

} // namespace

std::string thumbnailUrl(std::string_view videoId) {
    std::string url(kThumbnailPrefix);
    url += videoId;
    url += kThumbnailSuffix;
    return url;
}

std::string avatarUrl(std::string_view url, int pixels) {
    const auto at = url.rfind(kAvatarSizeMarker);
    if (at == std::string_view::npos) {
        return std::string(url);
    }
    auto end = at + kAvatarSizeMarker.size();
    while (end < url.size() && url[end] >= '0' && url[end] <= '9') {
        ++end;
    }
    std::string result(url.substr(0, at + kAvatarSizeMarker.size()));
    result += std::to_string(pixels);
    result += url.substr(end);
    return result;
}

Thumbnails::Thumbnails(const http::Client& http, TaskQueue& tasks, int decodeWidth,
                       std::size_t capacity)
    : m_http(http)
    , m_tasks(tasks)
    , m_decodeWidth(decodeWidth)
    , m_capacity(capacity) {}

Thumbnails::~Thumbnails() {
    for (auto& [url, entry] : m_entries) {
        entry.cancelled->store(true);
        glDeleteTextures(1, &entry.texture.id);
    }
}

void Thumbnails::beginFrame() {
    ++m_frame;
    for (auto& [url, entry] : m_entries) {
        if (entry.texture.id == 0 && m_frame - entry.lastUsedFrame > kAbandonAfterFrames) {
            entry.cancelled->store(true);
        }
    }
    std::erase_if(m_entries, [](const auto& item) {
        return item.second.texture.id == 0 && item.second.cancelled->load();
    });
    evict();
}

const Texture* Thumbnails::get(const std::string& url, int maxWidth) {
    if (url.empty()) {
        return nullptr;
    }
    auto [found, inserted] = m_entries.try_emplace(url);
    Entry& entry = found->second;
    entry.lastUsedFrame = m_frame;
    if (!inserted) {
        return entry.texture.id != 0 ? &entry.texture : nullptr;
    }

    entry.cancelled = std::make_shared<std::atomic<bool>>(false);
    m_tasks.run([this, url, cancelled = entry.cancelled, alive = std::weak_ptr(m_alive),
                 width = maxWidth > 0 ? maxWidth : m_decodeWidth, &http = m_http,
                 &tasks = m_tasks] {
        if (cancelled->load()) {
            return;
        }
        auto response = http.send({.url = url});
        auto decoded = std::make_shared<Decoded>();
        if (!response || !response->ok() || cancelled->load() ||
            !decode(response->body, width, *decoded)) {
            return;
        }
        tasks.post([this, url, alive, decoded] {
            if (alive.expired()) {
                return;
            }
            // The entry may have been abandoned and re-requested, and the retry already landed.
            const auto target = m_entries.find(url);
            if (target == m_entries.end() || target->second.texture.id != 0) {
                return;
            }
            Texture& texture = target->second.texture;
            glGenTextures(1, &texture.id);
            glBindTexture(GL_TEXTURE_2D, texture.id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, decoded->width, decoded->height, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, decoded->pixels.data());
            texture.width = decoded->width;
            texture.height = decoded->height;
            texture.readyAt = std::chrono::steady_clock::now();
        });
    });
    return nullptr;
}

void Thumbnails::evict() {
    std::size_t loaded = 0;
    for (const auto& [url, entry] : m_entries) {
        loaded += entry.texture.id != 0 ? 1 : 0;
    }
    while (loaded > m_capacity) {
        auto oldest = m_entries.end();
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if (it->second.texture.id != 0 && it->second.lastUsedFrame != m_frame &&
                (oldest == m_entries.end() ||
                 it->second.lastUsedFrame < oldest->second.lastUsedFrame)) {
                oldest = it;
            }
        }
        if (oldest == m_entries.end()) {
            return; // Everything loaded is on screen.
        }
        glDeleteTextures(1, &oldest->second.texture.id);
        m_entries.erase(oldest);
        --loaded;
    }
}

} // namespace sloptube::app
