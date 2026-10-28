// Downloads: the quality sheet of "Download", the list of downloaded videos (in "You") and the
// "You're offline" state that leads to it when there is no connection.

#include "app/App.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"

#include <format>

namespace sloptube::app {
namespace {

constexpr float kSheetTop = 24;     // dp
constexpr float kOptionRow = 56;    // dp
constexpr std::size_t kMaxRows = 6; // Qualities shown; the lowest ones are rarely wanted.
constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;

std::string megabytes(std::int64_t bytes) {
    const auto value =
        static_cast<std::int64_t>(static_cast<double>(bytes) / kBytesPerMegabyte + 0.5);
    return std::vformat(tr(Text::Megabytes), std::make_format_args(value));
}

std::int64_t streamBytes(const innertube::Player& player, const innertube::Streams& streams,
                         std::size_t quality) {
    std::int64_t bytes = player.formats[streams.qualities[quality].video].contentLength;
    if (streams.audio) {
        bytes += player.formats[*streams.audio].contentLength;
    }
    return bytes;
}

} // namespace

std::string App::downloadStatus(const Download& d) {
    switch (d.state) {
    case DownloadState::Done:
        return std::format("{} · {}", d.quality, megabytes(d.total));
    case DownloadState::Queued:
        return std::string(tr(Text::DownloadQueued));
    case DownloadState::Failed:
        return std::string(tr(Text::DownloadFailed));
    case DownloadState::Downloading: {
        const std::int64_t percent = d.total > 0 ? d.bytes * 100 / d.total : 0;
        return std::format("{} · {} / {}",
                           std::vformat(tr(Text::Percent), std::make_format_args(percent)),
                           megabytes(d.bytes), megabytes(d.total));
    }
    }
    return {};
}

void App::openDownload(const innertube::Item& item) {
    m_downloadSheet = DownloadSheetState{.item = item};
    openSheet(Sheet::Download);
    if (m_downloads->find(item.id)) {
        return; // The sheet offers to delete it instead.
    }
    m_downloadSheet.loading = true;
    m_tasks.run([this, id = item.id] {
        auto player = m_api.player(id);
        m_tasks.post([this, id, player = std::move(player)]() mutable {
            if (m_downloadSheet.item.id != id) {
                return;
            }
            m_downloadSheet.loading = false;
            if (player && !player->details.live) {
                m_downloadSheet.streams = innertube::selectStreams(*player);
                m_downloadSheet.player = std::move(*player);
            }
            m_downloadSheet.failed = !player || m_downloadSheet.streams.qualities.empty();
        });
    });
}

void App::openDownloads() {
    push(Screen{.route = Route::Downloads, .title = std::string(tr(Text::Downloads))});
}

float App::downloadSheetHeight(const Canvas& c) const {
    float rows = 2; // Loading, failure, or a download's status and "Delete".
    if (!m_downloads->find(m_downloadSheet.item.id) && !m_downloadSheet.loading &&
        !m_downloadSheet.failed) {
        rows = static_cast<float>(std::min(m_downloadSheet.streams.qualities.size(), kMaxRows));
    }
    return c.dp(kSheetTop) + c.lineHeight(theme::kSheetTitleSize) + c.dp(theme::kGutter) +
           c.dp(kOptionRow) * rows + c.dp(theme::kGutter);
}

void App::drawDownloadSheet(Frame& frame, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const float gutter = c.dp(theme::kGutter);
    const float row = c.dp(kOptionRow);
    const float iconX = min.x + gutter * 1.5f + c.dp(theme::kIconSize) * 0.5f;
    const float textX = min.x + gutter * 3 + c.dp(theme::kIconSize);
    const auto& sheet = m_downloadSheet;

    float y = min.y + c.dp(kSheetTop);
    c.with(theme::kHeadingWeight)
        .text({min.x + gutter * 1.5f, y}, theme::kSheetTitleSize, theme::kTextPrimary,
              tr(Text::DownloadVideo));
    y += c.lineHeight(theme::kSheetTitleSize) + gutter;

    const auto option = [&](Icon icon, std::string_view label, std::string_view detail) {
        const ImVec2 rowMin{min.x, y};
        const ImVec2 rowMax{max.x, y + row};
        drawPressed(frame, rowMin, rowMax);
        const float centerY = y + row * 0.5f;
        c.icon(icon, {iconX, centerY}, theme::kIconSize, theme::kTextPrimary);
        const float detailWidth = c.measure(theme::kCaptionSize, detail).x;
        c.line({textX, centerY - c.lineHeight(theme::kBodySize) * 0.5f},
               max.x - textX - detailWidth - gutter * 3, theme::kBodySize, theme::kTextPrimary,
               label);
        c.text({max.x - gutter * 1.5f - detailWidth,
                centerY - c.lineHeight(theme::kCaptionSize) * 0.5f},
               theme::kCaptionSize, theme::kTextSecondary, detail);
        y += row;
        return frame.touch.tapped(rowMin, rowMax);
    };

    if (const auto existing = m_downloads->find(sheet.item.id)) {
        const bool failed = existing->state == DownloadState::Failed;
        const std::string status = downloadStatus(*existing);
        if (option(existing->state == DownloadState::Done ? Icon::Downloaded : Icon::Download,
                   status, {}) &&
            failed) {
            m_downloads->retry(sheet.item.id);
        }
        if (existing->state == DownloadState::Downloading) {
            frame.animating = true;
        }
        if (option(Icon::Close, tr(Text::DeleteDownload), {})) {
            later([this, id = sheet.item.id] {
                m_downloads->remove(id);
                closeSheet();
            });
        }
        return;
    }
    if (sheet.loading) {
        c.spinner({(min.x + max.x) * 0.5f, y + row}, theme::kSpinnerSize, theme::kTextPrimary,
                  frame.time);
        frame.animating = true;
        return;
    }
    if (sheet.failed || !sheet.player) {
        c.line({min.x + gutter, y + row - c.lineHeight(theme::kBodySize) * 0.5f},
               max.x - min.x - gutter * 2, theme::kBodySize, theme::kTextSecondary,
               tr(Text::ActionFailed), TextAlign::Center);
        return;
    }
    const std::size_t count = std::min(sheet.streams.qualities.size(), kMaxRows);
    for (std::size_t i = 0; i < count; ++i) {
        const std::int64_t bytes = streamBytes(*sheet.player, sheet.streams, i);
        if (option(Icon::Download, sheet.streams.qualities[i].label,
                   bytes > 0 ? megabytes(bytes) : std::string())) {
            later([this, i] {
                m_downloads->start(m_downloadSheet.item, *m_downloadSheet.player, i);
                showToast(tr(Text::DownloadQueued));
                closeSheet();
            });
        }
    }
}

void App::drawDownloads(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const float bar = c.dp(theme::kTopBarHeight);
    const ImVec2 listMin{min.x, min.y + bar + c.dp(8)};

    // The list is rebuilt from the downloads every frame: a handful of entries.
    const auto downloads = m_downloads->list();
    Feed& feed = *screen.feed;
    feed.loaded = true;
    feed.page.items.clear();
    bool moving = false;
    for (const auto& d : downloads) {
        innertube::Item item = d.item;
        item.detail = downloadStatus(d);
        feed.page.items.push_back(std::move(item));
        moving |= d.state == DownloadState::Downloading;
    }
    frame.animating |= moving;

    if (downloads.empty()) {
        const float width = std::min(c.dp(280), max.x - min.x - c.dp(theme::kGutter) * 2);
        const float centerY = (listMin.y + max.y) * 0.5f;
        c.icon(Icon::Download, {(min.x + max.x) * 0.5f, centerY - c.dp(48)}, 48,
               theme::kTextSecondary);
        c.paragraph({(min.x + max.x - width) * 0.5f, centerY}, width, theme::kBodySize,
                    theme::kTextSecondary, tr(Text::NoDownloads), 4, TextAlign::Center);
    } else if (const auto tapped = drawList(frame, feed, ItemStyle::Row, listMin, max, {}, [] {})) {
        const Download& d = downloads[*tapped];
        if (d.state == DownloadState::Failed) {
            m_downloads->retry(d.item.id);
        } else {
            later([this, item = d.item, feed = screen.feed, index = *tapped] {
                openVideo(item, VideoSource{.feed = feed, .index = index});
            });
        }
    }
    drawTitleBar(frame, screen.title, {min.x, 0}, max.x - min.x);
}

void App::drawOffline(Frame& frame, const std::shared_ptr<Feed>& feed, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const float gutter = c.dp(theme::kGutter);
    const float width = std::min(c.dp(280), max.x - min.x - gutter * 2);
    const float x = (min.x + max.x - width) * 0.5f;
    float y = (min.y + max.y) * 0.5f - c.dp(96);

    c.icon(Icon::Download, {(min.x + max.x) * 0.5f, y}, 48, theme::kTextSecondary);
    y += c.dp(48);
    y += c.with(theme::kHeadingWeight)
             .paragraph({x, y}, width, theme::kSectionSize, theme::kTextPrimary, tr(Text::Offline),
                        2, TextAlign::Center);
    y += gutter * 0.5f;
    y += c.paragraph({x, y}, width, theme::kBodySize, theme::kTextSecondary, tr(Text::OfflineHint),
                     3, TextAlign::Center);
    y += gutter * 1.5f;

    const auto button = [&](Text label, bool primary) {
        const float buttonWidth = c.measure(theme::kBodySize, tr(label)).x + c.dp(32);
        const ImVec2 buttonMin{(min.x + max.x - buttonWidth) * 0.5f, y};
        const ImVec2 buttonMax{buttonMin.x + buttonWidth, y + c.dp(theme::kButtonHeight)};
        y = buttonMax.y + gutter;
        return pillButton(frame, tr(label), buttonMin, buttonMax, primary);
    };
    if (button(Text::GoToDownloads, true)) {
        later([this] { openDownloads(); });
    }
    if (button(Text::Retry, false) && feed->request) {
        load(feed, feed->request);
    }
}

} // namespace sloptube::app
