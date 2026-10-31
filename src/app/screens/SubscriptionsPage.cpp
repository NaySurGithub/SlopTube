// Subscriptions: the row of channels with new videos, the filter chips, then the feed; laid out as
// measured on the official app.

#include "app/App.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"

#include <algorithm>

namespace sloptube::app {
namespace {

constexpr float kChannelAvatar = 56; // dp
constexpr float kChannelStride = 72; // dp from one avatar to the next.
constexpr float kRowMargin = 12;     // dp before the first avatar and the first chip.
constexpr float kRowTop = 10;        // dp below the top bar.
constexpr float kNameGap = 14;       // dp between an avatar and its name.
constexpr float kAllWidth = 48;      // dp kept on the right for "All".
constexpr float kChipsTop = 108;     // dp from the top bar to the chips.
constexpr float kFeedTop = 152;      // dp from the top bar to the feed.
constexpr std::size_t kChannelsShown = 20;
constexpr float kNameOverhang = 6;  // dp a name may extend past its avatar.
constexpr float kNewDot = 8;        // dp, the blue dot of a channel with new videos.
constexpr float kDotOffset = 0.33f; // Of the avatar's size from its centre, down and right.

struct Filter {
    Text label;
    bool (*keep)(const innertube::Item&);
};

// "Today" from the server's own relative dates ("il y a 3 heures", "5 hours ago").
bool today(const innertube::Item& item) {
    std::string detail = item.detail;
    std::ranges::transform(detail, detail.begin(),
                           [](char ch) { return ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch; });
    for (const std::string_view unit : {"second", "minute", "heure", "hour"}) {
        if (detail.find(unit) != std::string::npos) {
            return true;
        }
    }
    return false;
}

constexpr std::array kSubscriptionFilters{
    Filter{Text::FilterAll, nullptr},
    Filter{Text::FilterToday, today},
    Filter{Text::FilterVideos,
           [](const innertube::Item& item) { return item.kind == innertube::ItemKind::Video; }},
    Filter{Text::Shorts,
           [](const innertube::Item& item) { return item.kind == innertube::ItemKind::Short; }},
    Filter{Text::LiveTab,
           [](const innertube::Item& item) { return item.kind == innertube::ItemKind::Live; }},
};

} // namespace

void App::filterSubscriptions(int filter) {
    m_subscriptionsFilter = filter;
    auto& feed = m_tabFeeds[std::to_underlying(Tab::Subscriptions)];
    feed = std::make_shared<Feed>();
    if (const auto keep = kSubscriptionFilters[static_cast<std::size_t>(filter)].keep) {
        feed->keep = keep;
    }
    load(feed, [this] { return m_api.feed(innertube::Feed::Subscriptions); });
}

void App::drawSubscriptions(Frame& frame, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const auto& feed = m_tabFeeds[std::to_underlying(Tab::Subscriptions)];

    // The channels of the feed, most recently active first, as the official row.
    std::vector<const innertube::Item*> channels;
    for (const auto& item : feed->page.items) {
        if (channels.size() >= kChannelsShown || item.channel.empty()) {
            continue;
        }
        const auto same = [&](const innertube::Item* other) {
            return item.channelId.empty() ? other->channel == item.channel
                                          : other->channelId == item.channelId;
        };
        if (std::ranges::none_of(channels, same)) {
            channels.push_back(&item);
        }
    }

    const ListHeader header{
        .height = c.dp(kFeedTop),
        .draw =
            [&](Frame& f, ImVec2 top, float w) {
                const float avatar = c.dp(kChannelAvatar);
                const float stride = c.dp(kChannelStride);
                const float rowTop = top.y + c.dp(kRowTop);
                const float rowBottom =
                    rowTop + avatar + c.dp(kNameGap) + c.lineHeight(theme::kCaptionSize);
                const ImVec2 rowMin{top.x, rowTop};
                const ImVec2 rowMax{top.x + w - c.dp(kAllWidth), rowBottom};
                m_channelsScroller.update(f.touch, rowMin, rowMax,
                                          c.dp(kRowMargin) * 2 +
                                              stride * static_cast<float>(channels.size()),
                                          f.seconds, c.dp(1));
                f.animating |= m_channelsScroller.moving();
                c.list()->PushClipRect(rowMin, rowMax, true);
                for (std::size_t i = 0; i < channels.size(); ++i) {
                    const float x = top.x + c.dp(kRowMargin) + stride * static_cast<float>(i) -
                                    m_channelsScroller.offset();
                    if (x + stride < rowMin.x || x > rowMax.x) {
                        continue;
                    }
                    const innertube::Item& item = *channels[i];
                    const ImVec2 center{x + avatar * 0.5f, rowTop + avatar * 0.5f};
                    drawAvatar(f, itemAvatar(f, item), item.channel, center, kChannelAvatar);
                    // A dot for a channel with videos not seen behind the bell yet.
                    const bool fresh =
                        std::ranges::any_of(feed->page.items, [&](const auto& other) {
                            return other.channel == item.channel &&
                                   !m_seenNotifications.contains(other.id);
                        });
                    if (fresh) {
                        const ImVec2 dot{center.x + avatar * kDotOffset,
                                         center.y + avatar * kDotOffset};
                        c.list()->AddCircleFilled(dot, c.dp(kNewDot) * 0.5f + c.dp(2),
                                                  theme::kBackground);
                        c.list()->AddCircleFilled(dot, c.dp(kNewDot) * 0.5f, theme::kLink);
                    }
                    // Names are cut at the avatar's width, as on the official app.
                    const float nameWidth = avatar + c.dp(kNameOverhang);
                    c.line({center.x - nameWidth * 0.5f, rowTop + avatar + c.dp(kNameGap)},
                           nameWidth, theme::kCaptionSize, theme::kTextPrimary, item.channel,
                           TextAlign::Center);
                    if (!m_channelsScroller.moving() &&
                        f.touch.tapped({x - c.dp(8), rowTop}, {x + avatar + c.dp(8), rowBottom})) {
                        later([this, id = item.channelId, name = item.channel] {
                            openChannel(id, name);
                        });
                    }
                }
                c.list()->PopClipRect();

                // "All": every subscribed channel.
                const Canvas medium = c.with(Weight::Medium);
                const ImVec2 all = medium.measure(theme::kBodySize, tr(Text::AllChannels));
                const float allX = top.x + w - c.dp(kRowMargin) - all.x;
                const float allY = rowTop + (avatar - all.y) * 0.5f;
                medium.text({allX, allY}, theme::kBodySize, theme::kLink, tr(Text::AllChannels));
                if (f.touch.tapped({rowMax.x, rowTop}, {top.x + w, rowTop + avatar})) {
                    later([this] {
                        Screen screen{.route = Route::AllSubscriptions,
                                      .title = std::string(tr(Text::AllSubscriptions))};
                        load(screen.feed, [this] { return m_api.feed(innertube::Feed::Channels); });
                        push(std::move(screen));
                    });
                }

                // The filter chips, scrolling sideways.
                const float chipsTop = top.y + c.dp(kChipsTop);
                const ImVec2 chipsMin{top.x, chipsTop};
                const ImVec2 chipsMax{top.x + w, chipsTop + c.dp(theme::kChipHeight)};
                float chipsWidth = c.dp(kRowMargin) * 2;
                for (const Filter& filter : kSubscriptionFilters) {
                    chipsWidth += medium.measure(theme::kBodySize, tr(filter.label)).x +
                                  c.dp(theme::kGutter) * 2 + c.dp(8);
                }
                m_subscriptionChipsScroller.update(f.touch, chipsMin, chipsMax, chipsWidth,
                                                   f.seconds, c.dp(1));
                f.animating |= m_subscriptionChipsScroller.moving();
                c.list()->PushClipRect(chipsMin, chipsMax, true);
                float x = top.x + c.dp(kRowMargin) - m_subscriptionChipsScroller.offset();
                for (std::size_t i = 0; i < kSubscriptionFilters.size(); ++i) {
                    const bool selected = m_subscriptionsFilter == static_cast<int>(i);
                    if (chipButton(f, tr(kSubscriptionFilters[i].label), {x, chipsTop}, selected,
                                   x) &&
                        !selected && !m_subscriptionChipsScroller.moving()) {
                        later([this, i] { filterSubscriptions(static_cast<int>(i)); });
                    }
                }
                c.list()->PopClipRect();
            },
    };
    drawFeed(frame, feed, ItemStyle::Card, min, max, header);
}

} // namespace sloptube::app
