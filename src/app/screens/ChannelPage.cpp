// Channel page: banner, identity, subscribe button, then the home tab's shelves or another tab
// (videos, shorts, live, playlists).

#include "app/App.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"
#include "app/ui/text/TextLayout.hpp"

#include <algorithm>
#include <format>

namespace sloptube::app {
namespace {

constexpr float kBannerAspect = 3.64f;   // Width over height of the mobile banner crop.
constexpr int kBannerDecodeWidth = 1080; // px, sharp across a phone screen.
constexpr int kAllLines = 1000;
constexpr std::string_view kChannelLink = "https://www.youtube.com/channel/";
constexpr std::string_view kHandleLink = "https://www.youtube.com/";

// The header, as measured on the official app (dp, sp).
constexpr float kMargin = 16;
constexpr float kAvatarTop = 12.5f; // From the banner to the avatar.
constexpr float kTextGap = 16;      // From the avatar to the name.
constexpr float kNameSize = 24;
constexpr float kNameTop = -2; // From the avatar's top to the name's line.
constexpr float kVerifiedSize = 17;
constexpr float kVerifiedGap = 7;
constexpr float kHandleTop = 33;      // From the avatar's top to the handle's line.
constexpr float kCountsPitch = 20;    // From the handle's line to the counts'.
constexpr float kDescriptionTop = 12; // From the avatar to the description.
constexpr float kLinksPitch = 27;     // From the description's line to the links'.
constexpr float kLinkIcon = 14;
constexpr float kLinkGap = 4;
constexpr float kButtonTop = 12; // From the links to the subscription button.
constexpr float kButtonHeight = 40;
constexpr float kBellIcon = 20;
constexpr float kBellGap = 9;
constexpr float kChevronGap = 11;
constexpr float kChevronIcon = 16;
constexpr float kTabsTop = 8; // From the button to the tab row.
constexpr float kTabsHeight = 48;
constexpr float kTabSize = 16;
constexpr float kTabSpacing = 25; // Between tab words.
constexpr float kUnderline = 2;
// The bar: back, then search and more on the right.
constexpr float kBackX = 28;
constexpr float kSearchRight = 72;
constexpr float kMoreRight = 24;
constexpr float kBarTitleX = 64;
// Home shelves, as measured on the official app.
constexpr float kSectionHeight = 53;   // From the top of a section to its content.
constexpr float kSectionTitleTop = 14; // From the top of a section to its title's line.
constexpr float kSectionChevron = 18;  // "›" after titles that open a tab.
constexpr float kBigTileWidth = 288;   // "For you": large tiles side by side.
constexpr float kBigTileSpacing = 16;
constexpr float kBigTitleTop = 10; // From a tile's picture to its title.
constexpr float kMetaTop = 4;      // From a title to its metadata.
constexpr float kShelfBottom = 20; // Below a shelf's last line.
constexpr float kRowPitch = 105;   // "Videos": rows of a picture and its title.
constexpr float kRowThumbnail = 160;
constexpr float kRowTextGap = 12;
constexpr float kRowMenuRight = 14;
constexpr float kRowTitleSize = 14;
constexpr std::size_t kRowsShown = 4; // Until the chevron below lists them all.
constexpr float kExpandHeight = 56;
constexpr float kGridGap = 1;        // Shorts: three columns edge to edge.
constexpr float kGridAspect = 1.68f; // Height over width of a Shorts cell.
constexpr std::size_t kGridShown = 6;
constexpr float kGridShade = 0.3f; // Of a Shorts cell darkened under its views.
constexpr ImU32 kShadeColor = IM_COL32(0, 0, 0, 0xb0);

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
    const Canvas bold = c.with(Weight::Bold);
    const Canvas medium = c.with(Weight::Medium);
    const float margin = c.dp(kMargin);
    const auto& channel = page.channel;
    auto& summary = page.channel.summary;
    const float right = origin.x + width;
    const float inner = width - margin * 2;

    // Banner, then the avatar beside the name, handle and counts, as measured on the official
    // app.
    float y = origin.y;
    if (!channel.banner.empty()) {
        const float banner = inner / kBannerAspect;
        drawThumbnail(frame, channel.banner, {origin.x + margin, y}, {right - margin, y + banner},
                      theme::kCardRadius, kBannerDecodeWidth);
        y += banner + c.dp(kAvatarTop);
    } else {
        y += c.dp(theme::kGutter);
    }
    const float avatar = c.dp(theme::kLargeAvatarSize);
    drawAvatar(frame, summary.avatar, summary.name,
               {origin.x + margin + avatar * 0.5f, y + avatar * 0.5f}, theme::kLargeAvatarSize);
    const float textX = origin.x + margin + avatar + c.dp(kTextGap);
    const float textWidth = right - margin - textX;
    const float nameTop = y + c.dp(kNameTop);
    const float verifiedSpace = channel.verified ? c.dp(kVerifiedGap + kVerifiedSize) : 0;
    const float nameWidth =
        std::min(bold.measure(kNameSize, summary.name).x, textWidth - verifiedSpace);
    bold.line({textX, nameTop}, nameWidth, kNameSize, theme::kTextPrimary, summary.name);
    if (channel.verified) {
        c.icon(Icon::Verified,
               {textX + nameWidth + c.dp(kVerifiedGap + kVerifiedSize * 0.5f),
                nameTop + c.lineHeight(kNameSize) * 0.5f},
               kVerifiedSize, theme::kTextSecondary);
    }
    const float handleTop = y + c.dp(kHandleTop);
    medium.line({textX, handleTop}, textWidth, theme::kCaptionSize, theme::kTextPrimary,
                summary.handle);
    std::string counts = summary.subscribers;
    if (!channel.videoCount.empty()) {
        counts += counts.empty() ? channel.videoCount : " • " + channel.videoCount;
    }
    c.line({textX, handleTop + c.dp(kCountsPitch)}, textWidth, theme::kCaptionSize,
           theme::kTextSecondary, counts);
    page.nameBottom = nameTop + c.lineHeight(kNameSize) - origin.y;
    y += avatar + c.dp(kDescriptionTop);

    // Description ("…more" opens it whole) and links.
    if (!channel.description.empty()) {
        const std::string more = std::string(text::kEllipsis) + std::string(tr(Text::More));
        const float moreWidth = medium.measure(theme::kCaptionSize, more).x;
        const float top = y;
        if (page.descriptionOpen) {
            y += c.paragraph({origin.x + margin, y}, inner, theme::kCaptionSize,
                             theme::kTextSecondary, channel.description, kAllLines);
        } else {
            // A pixel of slack: the measured width alone gets the line cut.
            const float used = std::min(c.measure(theme::kCaptionSize, channel.description).x + 1,
                                        inner - moreWidth - c.dp(4));
            c.line({origin.x + margin, y}, used, theme::kCaptionSize, theme::kTextSecondary,
                   channel.description);
            medium.text({origin.x + margin + used + c.dp(4), y}, theme::kCaptionSize,
                        theme::kTextPrimary, more);
            y += c.lineHeight(theme::kCaptionSize);
        }
        if (frame.touch.tapped({origin.x, top}, {right, y})) {
            page.descriptionOpen = !page.descriptionOpen;
        }
        y += c.dp(kLinksPitch) - c.lineHeight(theme::kCaptionSize);
    }
    if (!channel.links.empty()) {
        const float icon = c.dp(kLinkIcon);
        c.icon(Icon::Link,
               {origin.x + margin + icon * 0.5f, y + c.lineHeight(theme::kCaptionSize) * 0.5f},
               kLinkIcon, theme::kTextPrimary);
        const float linkX = origin.x + margin + icon + c.dp(kLinkGap);
        medium.line({linkX, y}, right - margin - linkX, theme::kCaptionSize, theme::kTextPrimary,
                    channel.links);
        const float bottom = y + c.lineHeight(theme::kCaptionSize);
        if (!channel.linkUrl.empty() && frame.touch.tapped({origin.x, y}, {right, bottom})) {
            m_platform.openUrl(channel.linkUrl);
        }
        y = bottom;
    }
    y += c.dp(kButtonTop);

    // Subscription: a bell pill once subscribed, "Subscribe" otherwise.
    const float button = c.dp(kButtonHeight);
    const ImVec2 buttonMin{origin.x + margin, y};
    const ImVec2 buttonMax{right - margin, y + button};
    if (summary.canSubscribe && summary.subscribed) {
        c.rect(buttonMin, buttonMax, theme::kSurface, kButtonHeight * 0.5f);
        drawPressed(frame, buttonMin, buttonMax, kButtonHeight * 0.5f);
        const std::string_view label = tr(Text::Subscribed);
        const float labelWidth = medium.measure(theme::kBodySize, label).x;
        const float group =
            c.dp(kBellIcon + kBellGap) + labelWidth + c.dp(kChevronGap + kChevronIcon);
        float x = (buttonMin.x + buttonMax.x - group) * 0.5f;
        const float centerY = y + button * 0.5f;
        c.icon(Icon::Bell, {x + c.dp(kBellIcon) * 0.5f, centerY}, kBellIcon, theme::kTextPrimary);
        x += c.dp(kBellIcon + kBellGap);
        medium.text({x, centerY - c.lineHeight(theme::kBodySize) * 0.5f}, theme::kBodySize,
                    theme::kTextPrimary, label);
        x += labelWidth + c.dp(kChevronGap);
        c.icon(Icon::ChevronDown, {x + c.dp(kChevronIcon) * 0.5f, centerY}, kChevronIcon,
               theme::kTextPrimary);
        if (frame.touch.tapped(buttonMin, buttonMax)) {
            toggleSubscription(summary);
        }
    } else {
        drawSubscribeButton(frame, summary, buttonMin, buttonMax);
    }
    y += button + c.dp(kTabsTop);

    page.tabsTop = y - origin.y;
    y += drawChannelTabs(frame, shared, {origin.x, y}, width);
    return y - origin.y;
}

float App::drawChannelTabs(Frame& frame, const std::shared_ptr<ChannelPage>& shared, ImVec2 origin,
                           float width) {
    ChannelPage& page = *shared;
    const Canvas& c = frame.canvas;
    const Canvas medium = c.with(Weight::Medium);
    const float margin = c.dp(kMargin);
    const float right = origin.x + width;
    const auto& channel = page.channel;
    float y = origin.y;
    c.rect(origin, {right, y + c.dp(kTabsHeight)}, theme::kBackground);
    // Tabs: words underlined when selected, over a divider, scrolling sideways when they do not
    // fit.
    std::vector<std::pair<Text, std::optional<innertube::ChannelTab>>> tabs{
        {Text::Home, std::nullopt}};
    constexpr std::array<std::pair<Text, innertube::ChannelTab>, 4> kTabs{{
        {Text::FilterVideos, innertube::ChannelTab::Videos},
        {Text::Shorts, innertube::ChannelTab::Shorts},
        {Text::LiveTab, innertube::ChannelTab::Live},
        {Text::Playlists, innertube::ChannelTab::Playlists},
    }};
    for (const auto& [label, tab] : kTabs) {
        if (channel.tabs.empty() || std::ranges::contains(channel.tabs, tab)) {
            tabs.emplace_back(label, tab);
        }
    }
    const float row = c.dp(kTabsHeight);
    const ImVec2 rowMin{origin.x, y};
    const ImVec2 rowMax{right, y + row};
    page.tabScroller.update(frame.touch, rowMin, rowMax, page.tabsWidth, frame.seconds, c.dp(1));
    frame.animating |= page.tabScroller.moving();
    c.list()->PushClipRect(rowMin, rowMax, true);
    float x = origin.x + margin - page.tabScroller.offset();
    const float start = x;
    for (const auto& [label, tab] : tabs) {
        const bool selected = page.tab == tab;
        const std::string_view text = tr(label);
        const float textWidth2 = medium.measure(kTabSize, text).x;
        medium.text({x, y + (row - c.lineHeight(kTabSize)) * 0.5f}, kTabSize,
                    selected ? theme::kTextPrimary : theme::kTextSecondary, text);
        if (selected) {
            c.rect({x, y + row - c.dp(kUnderline)}, {x + textWidth2, y + row}, theme::kTextPrimary);
        }
        const float spacing = c.dp(kTabSpacing);
        if (frame.touch.tapped({x - spacing * 0.5f, y},
                               {x + textWidth2 + spacing * 0.5f, y + row}) &&
            !selected && !page.tabScroller.moving()) {
            later([this, target = std::weak_ptr(shared), tab] {
                if (const auto page = target.lock()) {
                    selectChannelTab(*page, tab);
                    page->scroller.reset();
                }
            });
        }
        x += textWidth2 + spacing;
    }
    page.tabsWidth = x - start - c.dp(kTabSpacing) + margin * 2;
    c.list()->PopClipRect();
    y += row;
    c.rect({origin.x, y}, {right, y + std::max(1.0f, c.dp(0.4f))}, theme::kDivider);
    return y - origin.y + c.dp(1);
}

void App::drawChannelBar(Frame& frame, const ChannelPage& page, ImVec2 origin, float width,
                         bool named) {
    const Canvas& c = frame.canvas;
    const float height = m_insets.top + c.dp(theme::kTopBarHeight);
    c.rect(origin, {origin.x + width, origin.y + height}, theme::kBackground);
    const float centerY = origin.y + m_insets.top + c.dp(theme::kTopBarHeight) * 0.5f;
    if (iconButton(frame, Icon::Back, {origin.x + c.dp(kBackX), centerY}, theme::kTextPrimary)) {
        later([this] { goBack(); });
    }
    // The name shows once the header's has scrolled away.
    if (named) {
        const float textX = origin.x + c.dp(kBarTitleX);
        c.with(theme::kHeadingWeight)
            .line({textX, centerY - c.lineHeight(theme::kSectionSize) * 0.5f},
                  origin.x + width - c.dp(kSearchRight + 24) - textX, theme::kSectionSize,
                  theme::kTextPrimary, page.channel.summary.name);
    }
    if (iconButton(frame, Icon::Search, {origin.x + width - c.dp(kSearchRight), centerY},
                   theme::kTextPrimary)) {
        later([this] { openSearch(); });
    }
    // More: sharing the channel, the official menu's main entry.
    if (iconButton(frame, Icon::MoreVertical, {origin.x + width - c.dp(kMoreRight), centerY},
                   theme::kTextPrimary)) {
        const auto& summary = page.channel.summary;
        m_platform.share(summary.name, summary.handle.empty()
                                           ? std::format("{}{}", kChannelLink, summary.id)
                                           : std::format("{}{}", kHandleLink, summary.handle));
    }
}

void App::drawChannel(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const float width = max.x - min.x;
    const float bar = c.dp(theme::kTopBarHeight);
    // Shared: tab changes run later() and must find the page alive.
    const std::shared_ptr<ChannelPage> page = screen.channel;
    const ImVec2 contentMin{min.x, min.y + bar};

    if (page->loading || !page->error.empty()) {
        Feed status{.loading = page->loading, .loaded = !page->loading, .error = page->error};
        drawFeedStatus(frame, status, contentMin, max);
        drawChannelBar(frame, *page, {min.x, 0}, width, false);
        return;
    }

    // Once scrolled past them, the tabs stay under the bar: taps there are theirs.
    const auto& tabFeed = page->tab ? page->tabs[std::to_underlying(*page->tab)] : nullptr;
    const auto scrolled = [&] {
        return tabFeed ? (tabFeed->page.items.empty() ? 0 : tabFeed->scroller.offset())
                       : page->scroller.offset();
    };
    const bool pinned = scrolled() > page->tabsTop;
    Touch& touch = frame.touch;
    const bool wasBlocked = touch.blocked();
    const float tabsBottom = contentMin.y + c.dp(kTabsHeight);
    if (pinned && touch.down() && touch.start().y >= contentMin.y && touch.start().y < tabsBottom) {
        touch.setBlocked(true);
    }

    if (tabFeed) {
        const auto& feed = tabFeed;
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
    } else {
        drawChannelHome(frame, page, contentMin, max);
    }

    touch.setBlocked(wasBlocked);
    if (scrolled() > page->tabsTop) {
        (void)drawChannelTabs(frame, page, contentMin, width);
    }
    drawChannelBar(frame, *page, {min.x, 0}, width, scrolled() > page->nameBottom);
}

void App::drawChannelHome(Frame& frame, const std::shared_ptr<ChannelPage>& page, ImVec2 min,
                          ImVec2 max) {
    const Canvas& c = frame.canvas;
    const Canvas bold = c.with(Weight::Bold);
    const Canvas heading = c.with(theme::kHeadingWeight);
    const float width = max.x - min.x;
    const float margin = c.dp(kMargin);
    const auto& shelves = page->channel.shelves;
    page->expanded.resize(page->shelves.size());

    // Each shelf as the official app shows it: "For you" in large tiles, videos as rows, Shorts
    // in a grid, the others in small tiles.
    enum class Style : std::uint8_t {
        Big,
        Rows,
        Grid,
        Tiles
    };
    const auto styleOf = [&](std::size_t index) {
        const auto& items = page->shelves[index]->page.items;
        if (!items.empty() && std::ranges::all_of(items, [](const innertube::Item& item) {
                return item.kind == innertube::ItemKind::Short;
            })) {
            return Style::Grid;
        }
        if (index == 0) {
            return Style::Big;
        }
        return shelves[index].title == tr(Text::FilterVideos) ? Style::Rows : Style::Tiles;
    };
    const float bigWidth = c.dp(kBigTileWidth);
    const float bigHeight = bigWidth * theme::kAspect16x9 + c.dp(kBigTitleTop) +
                            c.lineHeight(theme::kTitleSize) * 2 + c.dp(kMetaTop) +
                            c.lineHeight(theme::kCaptionSize) + c.dp(kShelfBottom);
    const float cell = (width - c.dp(kGridGap) * 2) / 3;
    const auto rowsOf = [&](std::size_t index) {
        const std::size_t count = page->shelves[index]->page.items.size();
        return page->expanded[index] ? count : std::min(count, kRowsShown);
    };
    const auto heightOf = [&](std::size_t index) {
        const std::size_t count = page->shelves[index]->page.items.size();
        switch (styleOf(index)) {
        case Style::Big:
            return bigHeight;
        case Style::Rows:
            return c.dp(kRowPitch) * static_cast<float>(rowsOf(index)) +
                   (count > kRowsShown && !page->expanded[index] ? c.dp(kExpandHeight)
                                                                 : c.dp(kShelfBottom));
        case Style::Grid: {
            const auto rows = static_cast<float>((std::min(count, kGridShown) + 2) / 3);
            return (cell * kGridAspect + c.dp(kGridGap)) * rows + c.dp(kShelfBottom);
        }
        case Style::Tiles:
            break;
        }
        return shelfHeight(frame) + c.dp(kShelfBottom);
    };

    float content = page->headerHeight;
    for (std::size_t i = 0; i < page->shelves.size(); ++i) {
        content += c.dp(kSectionHeight) + heightOf(i);
    }
    page->scroller.update(frame.touch, min, max, content, frame.seconds, c.dp(1));
    frame.animating |= page->scroller.moving();
    const bool moving = page->scroller.moving();

    c.list()->PushClipRect(min, max, true);
    float y = min.y - page->scroller.offset();
    page->headerHeight = drawChannelHeader(frame, page, {min.x, y}, width);
    y += page->headerHeight;

    for (std::size_t i = 0; i < page->shelves.size(); ++i) {
        const auto& feed = page->shelves[i];
        const auto& items = feed->page.items;
        const Style style = styleOf(i);
        const float height = heightOf(i);
        if (y > max.y) {
            break;
        }
        if (y + c.dp(kSectionHeight) + height < min.y) {
            y += c.dp(kSectionHeight) + height;
            continue;
        }

        // The title; "Videos ›" opens the tab.
        const float titleY = y + c.dp(kSectionTitleTop);
        const auto& title = shelves[i].title;
        heading.line({min.x + margin, titleY}, width - margin * 2, theme::kSectionSize,
                     theme::kTextPrimary, title);
        if (style == Style::Rows) {
            const float titleWidth = heading.measure(theme::kSectionSize, title).x;
            const ImVec2 chevron{min.x + margin + titleWidth + c.dp(kSectionChevron) * 0.5f +
                                     c.dp(6),
                                 titleY + c.lineHeight(theme::kSectionSize) * 0.5f};
            c.icon(Icon::ChevronRight, chevron, kSectionChevron, theme::kTextPrimary);
            if (!moving &&
                frame.touch.tapped({min.x, y}, {chevron.x + c.dp(16), y + c.dp(kSectionHeight)})) {
                later([this, page] {
                    selectChannelTab(*page, innertube::ChannelTab::Videos);
                    page->scroller.reset();
                });
            }
        }
        y += c.dp(kSectionHeight);

        const auto open = [&](std::size_t index) {
            later([this, item = items[index], source = VideoSource{.feed = feed, .index = index}] {
                openItem(item, source);
            });
        };
        switch (style) {
        case Style::Big: {
            const ImVec2 rowMin{min.x, y};
            const ImVec2 rowMax{max.x, y + height};
            feed->scroller.update(
                frame.touch, rowMin, rowMax,
                margin * 2 + (bigWidth + c.dp(kBigTileSpacing)) * static_cast<float>(items.size()) -
                    c.dp(kBigTileSpacing),
                frame.seconds, c.dp(1));
            frame.animating |= feed->scroller.moving();
            for (std::size_t index = 0; index < items.size(); ++index) {
                const float x = min.x + margin +
                                (bigWidth + c.dp(kBigTileSpacing)) * static_cast<float>(index) -
                                feed->scroller.offset();
                if (x + bigWidth < min.x || x > max.x) {
                    continue;
                }
                const auto& item = items[index];
                const ImVec2 thumbMax{x + bigWidth, y + bigWidth * theme::kAspect16x9};
                drawThumbnail(frame, imageUrl(item), {x, y}, thumbMax, theme::kCardRadius);
                drawDurationBadge(frame, item.badge, thumbMax);
                const float textY = thumbMax.y + c.dp(kBigTitleTop);
                const float menu = c.dp(theme::kIconSize);
                const float titleHeight =
                    bold.paragraph({x, textY}, bigWidth - menu, theme::kTitleSize,
                                   theme::kTextPrimary, item.title, 2);
                const ImVec2 dots{x + bigWidth - menu * 0.3f,
                                  textY + c.lineHeight(theme::kTitleSize) * 0.5f};
                c.icon(Icon::MoreHorizontal, dots, 18, theme::kTextPrimary);
                drawCompactMeta(frame, item, {}, {x, textY + titleHeight + c.dp(kMetaTop)},
                                bigWidth - menu);
                if (moving || feed->scroller.moving()) {
                    continue;
                }
                if (frame.touch.tapped({dots.x - menu * 0.7f, textY - c.dp(4)},
                                       {dots.x + menu * 0.7f, textY + menu * 1.2f})) {
                    later([this, item] { openItemActions(item); });
                } else if (frame.touch.tapped({x, y}, {x + bigWidth, y + height})) {
                    open(index);
                }
            }
            break;
        }
        case Style::Rows: {
            const float thumbWidth = c.dp(kRowThumbnail);
            const float textX = min.x + margin + thumbWidth + c.dp(kRowTextGap);
            const float menuX = max.x - c.dp(kRowMenuRight);
            const std::size_t rows = rowsOf(i);
            for (std::size_t index = 0; index < rows; ++index) {
                const float top = y + c.dp(kRowPitch) * static_cast<float>(index);
                const auto& item = items[index];
                const ImVec2 rowMin{min.x, top};
                const ImVec2 rowMax{max.x, top + c.dp(kRowPitch)};
                drawPressed(frame, rowMin, rowMax);
                const ImVec2 thumbMax{min.x + margin + thumbWidth,
                                      top + thumbWidth * theme::kAspect16x9};
                drawThumbnail(frame, imageUrl(item), {min.x + margin, top}, thumbMax,
                              theme::kCornerRadius);
                drawDurationBadge(frame, item.badge, thumbMax);
                const float textWidth = menuX - c.dp(14) - textX;
                const float titleHeight = c.paragraph({textX, top}, textWidth, kRowTitleSize,
                                                      theme::kTextPrimary, item.title, 2);
                c.line({textX, top + titleHeight + c.dp(2)}, textWidth, theme::kCaptionSize,
                       theme::kTextSecondary, compactDetail(item.detail));
                c.icon(Icon::MoreHorizontal, {menuX, top + c.lineHeight(kRowTitleSize) * 0.5f}, 16,
                       theme::kTextPrimary);
                if (moving) {
                    continue;
                }
                if (frame.touch.tapped({menuX - c.dp(20), top}, {max.x, top + c.dp(40)})) {
                    later([this, item] { openItemActions(item); });
                } else if (frame.touch.tapped(rowMin, rowMax)) {
                    open(index);
                }
            }
            if (rows < items.size()) {
                const ImVec2 more{min.x + width * 0.5f,
                                  y + c.dp(kRowPitch) * static_cast<float>(rows) +
                                      c.dp(kExpandHeight) * 0.5f - c.dp(12)};
                c.icon(Icon::ChevronDown, more, 24, theme::kTextPrimary);
                if (!moving &&
                    frame.touch.tapped({min.x, more.y - c.dp(20)}, {max.x, more.y + c.dp(20)})) {
                    page->expanded[i] = true;
                }
            }
            break;
        }
        case Style::Grid: {
            const float cellHeight = cell * kGridAspect;
            const std::size_t shown = std::min(items.size(), kGridShown);
            // The home shelf has no view counts; the Shorts tab has them.
            auto& shortsTab = page->tabs[std::to_underlying(innertube::ChannelTab::Shorts)];
            if (!shortsTab) {
                shortsTab = std::make_shared<Feed>();
                load(shortsTab, [this, id = page->channel.summary.id] {
                    return m_api.channelTab(id, innertube::ChannelTab::Shorts);
                });
            }
            for (std::size_t index = 0; index < shown; ++index) {
                const float x = min.x + (cell + c.dp(kGridGap)) * static_cast<float>(index % 3);
                const float top = y + (cellHeight + c.dp(kGridGap)) * static_cast<float>(index / 3);
                const auto& item = items[index];
                drawThumbnail(frame, imageUrl(item), {x, top}, {x + cell, top + cellHeight}, 0);
                // A shade at the bottom keeps the views readable on light pictures.
                const float shade = top + cellHeight * (1 - kGridShade);
                c.list()->AddRectFilledMultiColor({x, shade}, {x + cell, top + cellHeight},
                                                  IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                                                  kShadeColor, kShadeColor);
                // The views, the first part of the detail ("194 k vues • ...").
                const auto counted =
                    std::ranges::find(shortsTab->page.items, item.id, &innertube::Item::id);
                const std::string_view detail =
                    counted != shortsTab->page.items.end() ? counted->detail : item.detail;
                const std::string_view views = detail.substr(0, detail.find(" \u2022 "));
                if (!views.empty()) {
                    c.with(Weight::Medium)
                        .line({x + c.dp(8),
                               top + cellHeight - c.dp(8) - c.lineHeight(theme::kCaptionSize)},
                              cell - c.dp(16), theme::kCaptionSize, theme::kTextPrimary, views);
                }
                c.icon(Icon::MoreHorizontal, {x + cell - c.dp(14), top + c.dp(12)}, 16,
                       theme::kTextPrimary);
                if (!moving && frame.touch.tapped({x, top}, {x + cell, top + cellHeight})) {
                    open(index);
                }
            }
            break;
        }
        case Style::Tiles:
            if (const auto tapped = drawShelf(frame, *feed, {min.x, y}, width)) {
                if (!moving) {
                    open(*tapped);
                }
            }
            break;
        }
        y += height;
    }
    c.list()->PopClipRect();
}

} // namespace sloptube::app
