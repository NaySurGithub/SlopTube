// Live streams: the chat, and moving through what the stream keeps (rewinding, back to live).

#include "app/App.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"

namespace sloptube::app {
namespace {

constexpr std::size_t kChatKept = 100; // Messages kept, newest last.
constexpr double kChatMinPoll = 1;     // s; the server asks for 3-5 s.
constexpr double kChatMaxPoll = 10;
constexpr double kChatRetry = 5;      // s after a failed poll.
constexpr int kChatMessageLines = 6;  // Longer messages are clamped.
constexpr float kChatAvatar = 24;     // dp
constexpr float kChatSpacing = 6;     // dp between messages.
constexpr float kChatFollowSlack = 4; // dp from the bottom still counted as following.
// Within this of the edge counts as live: playback starts a few segments behind it anyway.
constexpr std::int64_t kLiveEdgeSlackMs = 30'000;

} // namespace

std::optional<App::LiveTimeline> App::liveTimeline() const {
    if (!m_player.live()) {
        return std::nullopt;
    }
    const auto window = m_player.liveWindow();
    if (!window) {
        return LiveTimeline{}; // Not known yet: shown as live, without a bar.
    }
    const std::int64_t position =
        std::clamp(m_player.positionUs(), window->startUs, window->edgeUs);
    return LiveTimeline{
        .durationMs = (window->edgeUs - window->startUs) / 1000,
        .playedMs = (position - window->startUs) / 1000,
        .atEdge = (window->edgeUs - position) / 1000 < kLiveEdgeSlackMs,
    };
}

void App::seekLive(std::int64_t timelineMs) {
    if (const auto window = m_player.liveWindow()) {
        m_player.seek(window->startUs + timelineMs * 1000);
    }
}

void App::openLiveChat() {
    pollLiveChat();
    if (m_watch.chat) {
        m_watch.chat->following = true;
        openSheet(Sheet::LiveChat);
    }
}

void App::pollLiveChat() {
    if (!m_watch.details || !m_watch.details->live) {
        return;
    }
    if (!m_watch.chat) {
        // The uploader comes with the stream (or the page, whichever is in first).
        std::string channelId = m_watch.details->channelId;
        if (channelId.empty() && m_watch.info) {
            channelId = m_watch.info->channel.id;
        }
        if (channelId.empty()) {
            return;
        }
        m_watch.chat = std::make_shared<LiveChat>();
        m_watch.chat->token = innertube::Api::liveChatToken(channelId, m_watch.details->id);
    }
    const auto chat = m_watch.chat;
    if (chat->polling || chat->token.empty() || now() < chat->nextPollAt) {
        return;
    }
    chat->polling = true;
    m_tasks.run([this, target = std::weak_ptr(chat), token = chat->token] {
        auto page = m_api.liveChat(token);
        m_tasks.post([this, target, page = std::move(page)]() mutable {
            const auto chat = target.lock();
            if (!chat) {
                return;
            }
            chat->polling = false;
            if (!page) {
                chat->failed = chat->messages.empty();
                chat->nextPollAt = now() + kChatRetry;
                return;
            }
            chat->failed = false;
            for (auto& message : page->messages) {
                chat->messages.push_back(std::move(message));
            }
            if (chat->messages.size() > kChatKept) {
                const std::size_t dropped = chat->messages.size() - kChatKept;
                chat->messages.erase(chat->messages.begin(),
                                     chat->messages.begin() + static_cast<std::ptrdiff_t>(dropped));
                chat->heights.erase(chat->heights.begin(),
                                    chat->heights.begin() + static_cast<std::ptrdiff_t>(std::min(
                                                                dropped, chat->heights.size())));
            }
            chat->token = std::move(page->continuation);
            chat->nextPollAt =
                now() + std::clamp(page->pollMs / 1000.0, kChatMinPoll, kChatMaxPoll);
        });
    });
}

void App::drawLiveChat(Frame& frame, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const Canvas medium = c.with(Weight::Medium);
    const float gutter = c.dp(theme::kGutter);
    const float row = c.dp(theme::kTopBarHeight);
    const float centerY = min.y + row * 0.5f + c.dp(8);
    c.with(theme::kHeadingWeight)
        .text({min.x + gutter, centerY - c.lineHeight(theme::kSectionSize) * 0.5f},
              theme::kSectionSize, theme::kTextPrimary, tr(Text::LiveChat));
    if (iconButton(frame, Icon::Close, {max.x - row * 0.5f, centerY}, theme::kTextPrimary)) {
        later([this] { closeSheet(); });
    }
    const ImVec2 listMin{min.x, min.y + row + c.dp(8)};
    const auto chat = m_watch.chat;
    if (!chat || chat->messages.empty()) {
        Feed status{.loading = !chat || !chat->failed,
                    .loaded = chat && chat->failed,
                    .error = chat && chat->failed ? std::string(tr(Text::LiveChatUnavailable))
                                                  : std::string()};
        drawFeedStatus(frame, status, listMin, max, ItemStyle::Row);
        frame.animating = true; // Until the first messages.
        return;
    }

    // Each message: avatar, then the author and the text wrapped beside it.
    const float avatar = c.dp(kChatAvatar);
    const float textX = min.x + gutter * 1.5f + avatar;
    const float textWidth = max.x - gutter - textX;
    const float spacing = c.dp(kChatSpacing);
    // Messages are measured once, when they arrive (or when the width changes).
    std::vector<float>& heights = chat->heights;
    if (chat->measuredWidth != textWidth) {
        heights.clear();
        chat->measuredWidth = textWidth;
    }
    for (std::size_t i = heights.size(); i < chat->messages.size(); ++i) {
        const int lines = std::max(
            1, c.lineCount(textWidth, theme::kBodySize, chat->messages[i].text, kChatMessageLines));
        heights.push_back(
            std::max(avatar, c.lineHeight(theme::kCaptionSize) +
                                 c.lineHeight(theme::kBodySize) * static_cast<float>(lines)) +
            spacing);
    }
    float content = spacing;
    for (const float height : heights) {
        content += height;
    }
    if (chat->following) {
        chat->scroller.setOffset(content);
    }
    chat->scroller.update(frame.touch, listMin, max, content, frame.seconds, c.dp(1));
    frame.animating |= chat->scroller.moving();
    const float limit = std::max(0.0f, content - (max.y - listMin.y));
    chat->following = chat->scroller.offset() >= limit - c.dp(kChatFollowSlack);

    c.list()->PushClipRect(listMin, max, true);
    float y = listMin.y + spacing - chat->scroller.offset();
    for (std::size_t i = 0; i < chat->messages.size(); ++i) {
        const auto& message = chat->messages[i];
        if (y + heights[i] > listMin.y && y < max.y) {
            drawAvatar(frame, message.avatar, message.author,
                       {min.x + gutter + avatar * 0.5f, y + avatar * 0.5f}, kChatAvatar);
            std::string author = message.author;
            if (!message.amount.empty()) {
                author += "  " + message.amount;
            }
            medium.line({textX, y}, textWidth, theme::kCaptionSize,
                        message.amount.empty() ? theme::kTextSecondary : theme::kAccent, author);
            c.paragraph({textX, y + c.lineHeight(theme::kCaptionSize)}, textWidth, theme::kBodySize,
                        theme::kTextPrimary, message.text, kChatMessageLines);
        }
        y += heights[i];
    }
    c.list()->PopClipRect();
}

} // namespace sloptube::app
