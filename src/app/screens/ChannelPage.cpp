// Channel page: banner, identity, subscribe button and the channel's shelves.

#include "app/App.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"

namespace sloptube::app {
namespace {

constexpr float kBannerAspect = 6.2f;    // Width over height of the mobile banner crop.
constexpr int kBannerDecodeWidth = 1080; // px, sharp across a phone screen.
constexpr float kShelfTitleHeight = 44;  // dp

} // namespace

void App::openChannel(std::string channelId, std::string name) {
    if (channelId.empty()) {
        return;
    }
    auto page = std::make_shared<ChannelPage>();
    page->channel.summary.id = channelId;
    page->channel.summary.name = name;
    push(Screen{.route = Route::Channel, .argument = channelId, .title = name, .channel = page});

    m_tasks.run([this, target = std::weak_ptr(page), channelId] {
        auto channel = m_api.channel(channelId);
        m_tasks.post([this, target, channel = std::move(channel)]() mutable {
            const auto page = target.lock();
            if (!page) {
                return;
            }
            page->loading = false;
            if (!channel) {
                page->error = std::move(channel.error().message);
                return;
            }
            m_avatars->remember(channel->summary.id, channel->summary.avatar);
            for (auto& shelf : channel->shelves) {
                auto feed = std::make_shared<Feed>();
                feed->scroller = Scroller(Axis::Horizontal);
                feed->loaded = true;
                feed->page.items = std::move(shelf.items);
                page->shelves.push_back(std::move(feed));
            }
            page->channel = std::move(*channel);
        });
    });
}

void App::drawChannel(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const Canvas heading = c.with(theme::kHeadingWeight);
    const float width = max.x - min.x;
    const float gutter = c.dp(theme::kGutter);
    const float bar = c.dp(theme::kTopBarHeight);
    ChannelPage& page = *screen.channel;
    auto& summary = page.channel.summary;
    const ImVec2 contentMin{min.x, min.y + bar};

    if (page.loading || !page.error.empty()) {
        Feed status{.loading = page.loading, .loaded = !page.loading, .error = page.error};
        drawFeedStatus(frame, status, contentMin, max);
        drawTitleBar(frame, screen.title, {min.x, 0}, width);
        return;
    }

    const float banner = page.channel.banner.empty() ? 0 : (width - gutter * 2) / kBannerAspect;
    const float avatar = c.dp(theme::kLargeAvatarSize);
    const float button = c.dp(theme::kButtonHeight);
    const float shelf = c.dp(kShelfTitleHeight) + shelfHeight(frame) + gutter;
    const float content = gutter + (banner > 0 ? banner + gutter : 0) + avatar + gutter * 1.5f +
                          button + gutter * 2 + shelf * static_cast<float>(page.shelves.size());
    page.scroller.update(frame.touch, contentMin, max, content, frame.seconds, c.dp(1));
    frame.animating |= page.scroller.moving();

    c.list()->PushClipRect(contentMin, max, true);
    float y = contentMin.y + gutter - page.scroller.offset();
    if (banner > 0) {
        drawThumbnail(frame, page.channel.banner, {min.x + gutter, y}, {max.x - gutter, y + banner},
                      theme::kCardRadius, kBannerDecodeWidth);
        y += banner + gutter;
    }

    drawAvatar(frame, summary.avatar, summary.name,
               {min.x + gutter + avatar * 0.5f, y + avatar * 0.5f}, theme::kLargeAvatarSize);
    const float textX = min.x + gutter * 2 + avatar;
    const float textWidth = max.x - gutter - textX;
    const float textBlock = c.lineHeight(theme::kHeadlineSize) + c.lineHeight(theme::kCaptionSize);
    const float textTop = y + (avatar - textBlock) * 0.5f;
    heading.line({textX, textTop}, textWidth, theme::kHeadlineSize, theme::kTextPrimary,
                 summary.name);
    std::string byline = summary.handle;
    if (!summary.subscribers.empty()) {
        byline += byline.empty() ? summary.subscribers : " • " + summary.subscribers;
    }
    c.line({textX, textTop + c.lineHeight(theme::kHeadlineSize)}, textWidth, theme::kCaptionSize,
           theme::kTextSecondary, byline);
    y += avatar + gutter * 1.5f;

    drawSubscribeButton(frame, summary, {min.x + gutter, y}, {max.x - gutter, y + button});
    y += button + gutter * 2;

    for (std::size_t i = 0; i < page.shelves.size(); ++i) {
        const auto& feed = page.shelves[i];
        const auto& title = page.channel.shelves[i].title;
        heading.line({min.x + gutter,
                      y + (c.dp(kShelfTitleHeight) - c.lineHeight(theme::kSectionSize)) * 0.5f},
                     width - gutter * 2, theme::kSectionSize, theme::kTextPrimary, title);
        y += c.dp(kShelfTitleHeight);
        if (y < max.y && y + shelfHeight(frame) > contentMin.y) {
            if (const auto tapped = drawShelf(frame, *feed, {min.x, y}, width)) {
                later([this, item = feed->page.items[*tapped],
                       source = VideoSource{.feed = feed, .index = *tapped}] {
                    openItem(item, source);
                });
            }
        }
        y += shelfHeight(frame) + gutter;
    }
    c.list()->PopClipRect();

    drawTitleBar(frame, screen.title, {min.x, 0}, width);
}

} // namespace sloptube::app
