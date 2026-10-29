#pragma once

#include <GLES3/gl3.h>
#include <imgui.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sloptube::http {
class Client;
}

namespace sloptube::app {

class TaskQueue;
struct Decoded;

struct Texture {
    GLuint id = 0;
    int width = 0;
    int height = 0;
    std::chrono::steady_clock::time_point readyAt; // For fade-in.

    [[nodiscard]] ImTextureRef ref() const { return ImTextureRef(static_cast<ImTextureID>(id)); }
};

// Downloads, decodes (off the UI thread) and uploads remote images, keeping a bounded LRU set
// of GPU textures. All public methods run on the UI thread.
class Thumbnails {
public:
    Thumbnails(const http::Client& http, TaskQueue& tasks, int decodeWidth, std::size_t capacity);
    ~Thumbnails();

    Thumbnails(const Thumbnails&) = delete;
    Thumbnails& operator=(const Thumbnails&) = delete;

    // Texture for `url`, or nullptr while it loads (the load starts on first request). Images
    // wider than `maxWidth` (default: the constructor's decode width) are downscaled.
    [[nodiscard]] const Texture* get(const std::string& url, int maxWidth = 0);

    // Marks the start of a frame; textures untouched for a while become evictable, abandoned
    // downloads are cancelled, and a few decoded images are uploaded.
    void beginFrame();

    // Decoded images still wait for their upload: another frame should follow.
    [[nodiscard]] bool uploading() const { return !m_decoded.empty(); }

private:
    struct Entry {
        Texture texture;
        std::uint64_t lastUsedFrame = 0;
        std::shared_ptr<std::atomic<bool>> cancelled;
    };

    void evict();
    void upload(const std::string& url, const Decoded& decoded);

    const http::Client& m_http;
    TaskQueue& m_tasks;
    int m_decodeWidth;
    std::size_t m_capacity;
    std::uint64_t m_frame = 0;
    std::unordered_map<std::string, Entry> m_entries;
    // Decoded on workers, uploaded a few per frame: a burst landing after a fling would otherwise
    // be uploaded in one frame (about 1 ms per thumbnail).
    std::vector<std::pair<std::string, std::shared_ptr<Decoded>>> m_decoded;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

[[nodiscard]] std::string thumbnailUrl(std::string_view videoId);

// `url` of a channel avatar or account photo re-targeted to `pixels` square (they are served at
// any size through a "=s<size>" suffix).
[[nodiscard]] std::string avatarUrl(std::string_view url, int pixels);

} // namespace sloptube::app
