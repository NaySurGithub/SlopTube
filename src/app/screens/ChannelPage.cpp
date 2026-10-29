// Channel page: banner, identity, subscribe button, then the home tab's shelves or another tab
// (videos, shorts, live, playlists).

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

void App::selectChannelTab(ChannelPage& page, std::optional<innertube::ChannelTab> tab) {
    page.tab = tab;
    if (!tab) {
        return;
    }
    auto& feed = page.tabs[std::to_underlying(*tab)];
    if (!feed) {
        feed = std::make_shared<Feed>();
        load(feed, [this, id = page.channel.summary.id, tab = *tab] {
            return m_api.channelTab(id, tab);
        });
    }
}

float App::drawChannelHeader(Frame& frame, const std::shared_ptr<ChannelPage>& shared,
                             ImVec2 origin, float width) {
    ChannelPage& page = *shared;
    const Canvas& c = frame.canvas;
    const Canvas heading = c.with(theme::kHeadingWeight);
    const float gutter = c.dp(theme::kGutter);
    auto& summary = page.channel.summary;
    const float banner = page.channel.banner.empty() ? 0 : (width - gutter * 2) / kBannerAspect;
    const float avatar = c.dp(theme::kLargeAvatarSize);
    const float button = c.dp(theme::kButtonHeight);
    const float right = origin.x + width;

    float y = origin.y + gutter;
    if (banner > 0) {
        drawThumbnail(frame, page.channel.banner, {origin.x + gutter, y},
                      {right - gutter, y + banner}, theme::kCardRadius, kBannerDecodeWidth);
        y += banner + gutter;
    }

    drawAvatar(frame, summary.avatar, summary.name,
               {origin.x + gutter + avatar * 0.5f, y + avatar * 0.5f}, theme::kLargeAvatarSize);
    const float textX = origin.x + gutter * 2 + avatar;
    const float textWidth = right - gutter - textX;
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

    drawSubscribeButton(frame, summary, {origin.x + gutter, y}, {right - gutter, y + button});
    y += button + gutter * 1.5f;

    // Tabs, scrolling sideways when they do not fit, as in the official app.
    constexpr std::array<std::pair<Text, std::optional<innertube::ChannelTab>>, 5> kTabs{{
        {Text::Home, std::nullopt},
        {Text::FilterVideos, innertube::ChannelTab::Videos},
        {Text::Shorts, innertube::ChannelTab::Shorts},
        {Text::LiveTab, innertube::ChannelTab::Live},
        {Text::Playlists, innertube::ChannelTab::Playlists},
    }};
    const float chip = c.dp(theme::kChipHeight);
    const ImVec2 rowMin{origin.x, y};
    const ImVec2 rowMax{right, y + chip};
    page.tabScroller.update(frame.touch, rowMin, rowMax, page.tabsWidth, frame.seconds, c.dp(1));
    frame.animating |= page.tabScroller.moving();
    c.list()->PushClipRect(rowMin, rowMax, true);
    float x = origin.x + gutter - page.tabScroller.offset();
    const float start = x;
    for (const auto& [label, tab] : kTabs) {
        const bool selected = page.tab == tab;
        if (chipButton(frame, tr(label), {x, y}, selected, x) && !selected &&
            !page.tabScroller.moving()) {
            later([this, target = std::weak_ptr(shared), tab] {
                if (const auto page = target.lock()) {
                    selectChannelTab(*page, tab);
                    page->scroller.reset();
                }
            });
        }
    }
    page.tabsWidth = x - start + gutter;
    c.list()->PopClipRect();
    y += chip + gutter;
    return y - origin.y;
}

void App::drawChannel(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const Canvas heading = c.with(theme::kHeadingWeight);
    const float width = max.x - min.x;
    const float gutter = c.dp(theme::kGutter);
    const float bar = c.dp(theme::kTopBarHeight);
    // Shared: tab changes run later() and must find the page alive.
    const std::shared_ptr<ChannelPage> page = screen.channel;
    const ImVec2 contentMin{min.x, min.y + bar};

    if (page->loading || !page->error.empty()) {
        Feed status{.loading = page->loading, .loaded = !page->loading, .error = page->error};
        drawFeedStatus(frame, status, contentMin, max);
        drawTitleBar(frame, screen.title, {min.x, 0}, width);
        return;
    }

    // A tab: its list under the header, scrolling together.
    if (page->tab) {
        const auto& feed = page->tabs[std::to_underlying(*page->tab)];
        // Tab pages do not name the channel they list; it is this one.
        for (auto& item : feed->page.items) {
            if (item.channel.empty() && item.kind != innertube::ItemKind::Playlist) {
                item.channel = page->channel.summary.name;
                item.channelId = page->channel.summary.id;
            }
        }
        const ListHeader header{
            .height = page->headerHeight,
            .draw = [this,
                     page](Frame& f, ImVec2 origin,
                           float w) { page->headerHeight = drawChannelHeader(f, page, origin, w); },
        };
        const ItemStyle style =
            *page->tab == innertube::ChannelTab::Playlists ? ItemStyle::Row : ItemStyle::Card;
        if (feed->page.items.empty()) {
            // The header stays while the tab loads, is empty or failed.
            const float top = contentMin.y;
            page->headerHeight = drawChannelHeader(frame, page, {min.x, top}, width);
            if (drawFeedStatus(frame, *feed, {min.x, top + page->headerHeight}, max, style) &&
                feed->request) {
                load(feed, feed->request);
            }
        } else {
            drawFeed(frame, feed, style, contentMin, max, header);
        }
        drawTitleBar(frame, screen.title, {min.x, 0}, width);
        return;
    }

    // Home: the header, then every shelf of the channel's home tab.
    const float shelf = c.dp(kShelfTitleHeight) + shelfHeight(frame) + gutter;
    const float content =
        page->headerHeight + gutter + shelf * static_cast<float>(page->shelves.size());
    page->scroller.update(frame.touch, contentMin, max, content, frame.seconds, c.dp(1));
    frame.animating |= page->scroller.moving();

    c.list()->PushClipRect(contentMin, max, true);
    float y = contentMin.y - page->scroller.offset();
    page->headerHeight = drawChannelHeader(frame, page, {min.x, y}, width);
    y += page->headerHeight + gutter;

    for (std::size_t i = 0; i < page->shelves.size(); ++i) {
        const auto& feed = page->shelves[i];
        const auto& title = page->channel.shelves[i].title;
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
