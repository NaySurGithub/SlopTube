#include "app/downloads/Downloads.hpp"

#include "app/media/Log.hpp"
#include "app/util/Files.hpp"
#include "app/util/TaskQueue.hpp"
#include "core/http/Client.hpp"
#include "core/innertube/Api.hpp"
#include "core/innertube/Captions.hpp"
#include "core/innertube/Streams.hpp"
#include "core/json/Json.hpp"

#include <android/log.h>

#include <algorithm>
#include <fstream>
#include <system_error>

namespace sloptube::app {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kManifest = "manifest.json";
constexpr std::string_view kVideoFile = "video";
constexpr std::string_view kAudioFile = "audio";
constexpr std::string_view kThumbFile = "thumb";
constexpr std::string_view kAvatarFile = "avatar";
constexpr std::string_view kCaptionsDir = "captions";
constexpr std::string_view kPartSuffix = ".part";
// Smaller than the CDN's cap so progress moves often; large enough to amortise latency.
constexpr std::int64_t kChunkBytes = 4 * 1024 * 1024;
constexpr int kChunkAttempts = 3;
constexpr std::size_t kMaxCaptionTracks = 12;

std::string fileUrl(const fs::path& path) {
    // The "?" lets query parameters (a range, a caption format) be appended as usual.
    return std::string(http::kFileScheme) + path.string() + "?";
}

void writeFormat(json::Writer& out, std::string_view key, const innertube::Format& format) {
    out.beginObject(key)
        .field("itag", static_cast<std::int64_t>(format.itag))
        .field("mimeType", format.mimeType)
        .field("width", static_cast<std::int64_t>(format.width))
        .field("height", static_cast<std::int64_t>(format.height))
        .field("fps", static_cast<std::int64_t>(format.fps))
        .field("bitrate", format.bitrate)
        .field("contentLength", format.contentLength)
        .field("qualityLabel", format.qualityLabel)
        .endObject();
}

innertube::Format readFormat(json::Element root, std::string_view pointer) {
    const auto get = [&](std::string_view field) {
        return std::string(pointer) + "/" + std::string(field);
    };
    return {
        .itag = static_cast<int>(json::integer(root, get("itag"))),
        .mimeType = std::string(json::string(root, get("mimeType"))),
        .width = static_cast<int>(json::integer(root, get("width"))),
        .height = static_cast<int>(json::integer(root, get("height"))),
        .fps = static_cast<int>(json::integer(root, get("fps"))),
        .bitrate = json::integer(root, get("bitrate")),
        .contentLength = json::integer(root, get("contentLength")),
        .qualityLabel = std::string(json::string(root, get("qualityLabel"))),
    };
}

} // namespace

struct Downloads::Job {
    Download download;
    fs::path directory;
    innertube::VideoDetails details;
    innertube::Format video;
    std::optional<innertube::Format> audio;
    std::vector<innertube::CaptionTrack> captions; // Local tracks; `url` unused.
    bool resolved = false; // Remote URLs known (they expire, so never stored).
};

Downloads::Downloads(const http::Client& http, innertube::Api& api, TaskQueue& tasks, fs::path root)
    : m_http(http)
    , m_api(api)
    , m_tasks(tasks)
    , m_root(std::move(root)) {
    std::error_code error;
    fs::create_directories(m_root, error);
    load();
    m_thread = std::thread([this] { workerLoop(); });
}

Downloads::~Downloads() {
    {
        const std::scoped_lock lock(m_mutex);
        m_stopping = true;
    }
    m_wake.notify_all();
    m_thread.join();
}

void Downloads::load() {
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(m_root, error)) {
        auto text = readText(entry.path() / kManifest);
        if (!text) {
            continue;
        }
        auto parsed = json::parse(*text);
        if (!parsed) {
            continue;
        }
        const json::Element root = *parsed;
        auto job = std::make_unique<Job>();
        job->directory = entry.path();
        Download& d = job->download;
        d.item = {
            .kind = innertube::ItemKind::Video,
            .id = std::string(json::string(root, "/item/id")),
            .title = std::string(json::string(root, "/item/title")),
            .channel = std::string(json::string(root, "/item/channel")),
            .detail = std::string(json::string(root, "/item/detail")),
            .badge = std::string(json::string(root, "/item/badge")),
            .thumbnail = std::string(json::string(root, "/item/thumbnail")),
            .channelId = std::string(json::string(root, "/item/channelId")),
            .avatar = std::string(json::string(root, "/item/avatar")),
        };
        d.quality = std::string(json::string(root, "/quality"));
        d.state = json::boolean(root, "/complete") ? DownloadState::Done : DownloadState::Queued;
        job->details = {
            .id = d.item.id,
            .title = d.item.title,
            .channel = d.item.channel,
            .channelId = d.item.channelId,
            .durationSeconds = json::integer(root, "/durationSeconds"),
        };
        job->video = readFormat(root, "/video");
        if (json::has(root, "/audio")) {
            job->audio = readFormat(root, "/audio");
        }
        simdjson::dom::array tracks;
        if (root.at_pointer("/captions").get(tracks) == simdjson::SUCCESS) {
            for (const auto track : tracks) {
                job->captions.push_back({
                    .language = std::string(json::string(track, "/language")),
                    .name = std::string(json::string(track, "/name")),
                    .autoGenerated = json::boolean(track, "/auto"),
                });
            }
        }
        d.total = job->video.contentLength + (job->audio ? job->audio->contentLength : 0);
        d.bytes = d.state == DownloadState::Done ? d.total : 0;
        if (d.item.id.empty() || job->video.itag == 0) {
            continue;
        }
        m_jobs.push_back(std::move(job));
    }
    // Directory order is arbitrary; the manifest's write time orders them, newest first.
    std::ranges::sort(m_jobs, [](const auto& a, const auto& b) {
        std::error_code e;
        return fs::last_write_time(a->directory / kManifest, e) >
               fs::last_write_time(b->directory / kManifest, e);
    });
}

void Downloads::saveManifest(const Job& job) const {
    const Download& d = job.download;
    json::Writer out;
    out.beginObject()
        .beginObject("item")
        .field("id", d.item.id)
        .field("title", d.item.title)
        .field("channel", d.item.channel)
        .field("detail", d.item.detail)
        .field("badge", d.item.badge)
        .field("thumbnail", d.item.thumbnail)
        .field("channelId", d.item.channelId)
        .field("avatar", d.item.avatar)
        .endObject()
        .field("quality", d.quality)
        .field("durationSeconds", job.details.durationSeconds)
        .field("complete", d.state == DownloadState::Done);
    writeFormat(out, "video", job.video);
    if (job.audio) {
        writeFormat(out, "audio", *job.audio);
    }
    out.beginArray("captions");
    for (const auto& track : job.captions) {
        out.beginObject()
            .field("language", track.language)
            .field("name", track.name)
            .field("auto", track.autoGenerated)
            .endObject();
    }
    out.endArray().endObject();
    if (!writeText(job.directory / kManifest, out.take())) {
        __android_log_print(ANDROID_LOG_WARN, media::kLogTag,
                            "download: cannot write the manifest of %s", d.item.id.c_str());
    }
}

void Downloads::start(const innertube::Item& item, const innertube::Player& player,
                      std::size_t quality) {
    const innertube::Streams streams = innertube::selectStreams(player);
    if (quality >= streams.qualities.size() || player.details.live) {
        return; // A broadcast has no end to download up to.
    }
    if (const auto existing = find(item.id)) {
        if (existing->state == DownloadState::Downloading) {
            return; // Removing it first is the way to change its quality.
        }
        remove(item.id); // A new quality replaces the old download.
    }
    auto job = std::make_unique<Job>();
    job->directory = m_root / item.id;
    job->details = player.details;
    job->video = player.formats[streams.qualities[quality].video];
    if (streams.audio) {
        job->audio = player.formats[*streams.audio];
    }
    job->resolved = true;
    Download& d = job->download;
    d.item = item;
    d.item.kind = innertube::ItemKind::Video;
    d.quality = streams.qualities[quality].label;
    d.total = job->video.contentLength + (job->audio ? job->audio->contentLength : 0);
    std::error_code error;
    fs::create_directories(job->directory / kCaptionsDir, error);
    saveManifest(*job);
    {
        const std::scoped_lock lock(m_mutex);
        m_jobs.push_front(std::move(job));
    }
    m_wake.notify_all();
    changed();
}

void Downloads::retry(const std::string& videoId) {
    {
        const std::scoped_lock lock(m_mutex);
        Job* job = findJob(videoId);
        if (!job || job->download.state != DownloadState::Failed || m_cancelled == videoId) {
            return;
        }
        job->download.state = DownloadState::Queued;
        job->resolved = false;
    }
    m_wake.notify_all();
    changed();
}

void Downloads::remove(const std::string& videoId) {
    std::unique_ptr<Job> removed;
    {
        std::unique_lock lock(m_mutex);
        const auto found = std::ranges::find_if(
            m_jobs, [&](const auto& job) { return job->download.item.id == videoId; });
        if (found == m_jobs.end()) {
            return;
        }
        if ((*found)->download.state == DownloadState::Downloading) {
            // The worker owns the job until its transfer stops; it deletes the files then.
            m_cancelled = videoId;
            (*found)->download.state = DownloadState::Failed;
            m_wake.notify_all();
            return;
        }
        removed = std::move(*found);
        m_jobs.erase(found);
    }
    std::error_code error;
    fs::remove_all(removed->directory, error);
    changed();
}

std::vector<Download> Downloads::list() const {
    const std::scoped_lock lock(m_mutex);
    std::vector<Download> out;
    out.reserve(m_jobs.size());
    for (const auto& job : m_jobs) {
        if (m_cancelled != job->download.item.id) {
            out.push_back(job->download);
        }
    }
    return out;
}

std::optional<Download> Downloads::find(const std::string& videoId) const {
    const std::scoped_lock lock(m_mutex);
    const Job* job = findJob(videoId);
    if (!job || m_cancelled == videoId) {
        return std::nullopt;
    }
    return job->download;
}

bool Downloads::busy() const {
    const std::scoped_lock lock(m_mutex);
    return std::ranges::any_of(m_jobs, [](const auto& job) {
        return job->download.state == DownloadState::Downloading ||
               job->download.state == DownloadState::Queued;
    });
}

std::optional<innertube::Player> Downloads::localPlayer(const std::string& videoId) const {
    const std::scoped_lock lock(m_mutex);
    const Job* job = findJob(videoId);
    if (!job || job->download.state != DownloadState::Done) {
        return std::nullopt;
    }
    innertube::Player player{.details = job->details};
    player.formats.push_back(job->video);
    player.formats.back().url = fileUrl(job->directory / kVideoFile);
    if (job->audio) {
        player.formats.push_back(*job->audio);
        player.formats.back().url = fileUrl(job->directory / kAudioFile);
    }
    return player;
}

std::optional<innertube::Captions> Downloads::localCaptions(const std::string& videoId) const {
    const std::scoped_lock lock(m_mutex);
    const Job* job = findJob(videoId);
    if (!job || job->download.state != DownloadState::Done) {
        return std::nullopt;
    }
    innertube::Captions captions;
    for (std::size_t i = 0; i < job->captions.size(); ++i) {
        auto track = job->captions[i];
        track.url = fileUrl(job->directory / kCaptionsDir / std::to_string(i));
        captions.tracks.push_back(std::move(track));
    }
    return captions;
}

Downloads::Job* Downloads::findJob(const std::string& videoId) {
    const auto found = std::ranges::find_if(
        m_jobs, [&](const auto& job) { return job->download.item.id == videoId; });
    return found == m_jobs.end() ? nullptr : found->get();
}

const Downloads::Job* Downloads::findJob(const std::string& videoId) const {
    return const_cast<Downloads*>(this)->findJob(videoId);
}

void Downloads::changed() const {
    m_tasks.post([] {}); // Any posted task wakes the looper for a frame.
}

void Downloads::workerLoop() {
    std::unique_lock lock(m_mutex);
    while (true) {
        // The oldest queued download goes first.
        const auto next = std::find_if(m_jobs.rbegin(), m_jobs.rend(), [](const auto& job) {
            return job->download.state == DownloadState::Queued;
        });
        if (m_stopping) {
            return;
        }
        if (next == m_jobs.rend()) {
            m_wake.wait(lock);
            continue;
        }
        Job& job = **next;
        job.download.state = DownloadState::Downloading;
        lock.unlock();
        changed();
        run(job);
        lock.lock();

        const std::string id = job.download.item.id;
        if (m_cancelled == id) {
            m_cancelled.clear();
            const auto found = std::ranges::find_if(
                m_jobs, [&](const auto& other) { return other.get() == &job; });
            const fs::path directory = job.directory;
            m_jobs.erase(found);
            lock.unlock();
            std::error_code error;
            fs::remove_all(directory, error);
            lock.lock();
        }
        changed();
    }
}

void Downloads::run(Job& job) {
    const std::string id = job.download.item.id;
    const auto finish = [&](DownloadState state) {
        const std::scoped_lock lock(m_mutex);
        if (m_cancelled != id) {
            job.download.state = state;
        }
    };

    // Stream URLs expire within hours: a resumed download asks for fresh ones.
    if (!job.resolved) {
        auto player = m_api.player(id);
        const auto withItag = [&](int itag) -> const innertube::Format* {
            if (!player) {
                return nullptr;
            }
            const auto found = std::ranges::find(player->formats, itag, &innertube::Format::itag);
            return found == player->formats.end() ? nullptr : &*found;
        };
        const innertube::Format* video = withItag(job.video.itag);
        const innertube::Format* audio = job.audio ? withItag(job.audio->itag) : nullptr;
        if (!video || (job.audio && !audio)) {
            finish(DownloadState::Failed);
            return;
        }
        job.video.url = video->url;
        if (audio) {
            job.audio->url = audio->url;
        }
        job.resolved = true;
    }

    // Images shown offline, then subtitles, then the streams.
    for (auto [url, file] : {std::pair{&job.download.item.thumbnail, kThumbFile},
                             std::pair{&job.download.item.avatar, kAvatarFile}}) {
        if (url->empty() || url->starts_with(http::kFileScheme)) {
            continue;
        }
        if (auto image = m_http.send({.url = *url});
            image && image->ok() && writeText(job.directory / file, image->body)) {
            const std::scoped_lock lock(m_mutex);
            *url = fileUrl(job.directory / file);
        }
    }
    if (job.captions.empty()) {
        if (auto captions = m_api.captions(id)) {
            for (const auto& track : captions->tracks) {
                if (job.captions.size() == kMaxCaptionTracks) {
                    break;
                }
                auto cues = m_http.send({.url = innertube::timedTextUrl(track, {})});
                if (!cues || !cues->ok() ||
                    !writeText(job.directory / kCaptionsDir / std::to_string(job.captions.size()),
                               cues->body)) {
                    continue;
                }
                job.captions.push_back({.language = track.language,
                                        .name = track.name,
                                        .autoGenerated = track.autoGenerated});
            }
        }
    }
    saveManifest(job);

    if (!fetchFile(job, job.video, job.directory / kVideoFile) ||
        (job.audio && !fetchFile(job, *job.audio, job.directory / kAudioFile))) {
        finish(DownloadState::Failed);
        return;
    }
    {
        const std::scoped_lock lock(m_mutex);
        if (m_cancelled == id) {
            return;
        }
        job.download.state = DownloadState::Done;
        job.download.bytes = job.download.total;
    }
    saveManifest(job);
}

bool Downloads::fetchFile(Job& job, const innertube::Format& format, const fs::path& path) {
    std::error_code error;
    if (fs::exists(path, error)) {
        return true; // Finished earlier.
    }
    const fs::path part = path.string() + std::string(kPartSuffix);
    std::int64_t offset =
        fs::exists(part, error) ? static_cast<std::int64_t>(fs::file_size(part, error)) : 0;
    const std::string id = job.download.item.id;
    const auto otherBytes = [&] {
        // Bytes of the stream fetched before this one.
        return &format == &job.video ? 0 : job.video.contentLength;
    };
    std::ofstream file(part, std::ios::binary | std::ios::app);
    if (!file) {
        return false;
    }
    while (format.contentLength == 0 || offset < format.contentLength) {
        {
            const std::scoped_lock lock(m_mutex);
            if (m_cancelled == id || m_stopping) {
                return false;
            }
            job.download.bytes = otherBytes() + offset;
        }
        changed();
        std::string url = format.url;
        url += http::kRangeParameter;
        url += std::to_string(offset);
        url += '-';
        url += std::to_string(offset + kChunkBytes - 1);
        std::optional<std::string> body;
        for (int attempt = 0; attempt < kChunkAttempts && !body; ++attempt) {
            if (auto response = m_http.send({.url = url}); response && response->ok()) {
                body = std::move(response->body);
            }
        }
        if (!body) {
            return false;
        }
        file.write(body->data(), static_cast<std::streamsize>(body->size()));
        if (!file) {
            return false;
        }
        offset += static_cast<std::int64_t>(body->size());
        if (static_cast<std::int64_t>(body->size()) < kChunkBytes) {
            break; // The end of a stream whose length was unknown.
        }
    }
    file.close();
    fs::rename(part, path, error);
    return !error;
}

} // namespace sloptube::app
