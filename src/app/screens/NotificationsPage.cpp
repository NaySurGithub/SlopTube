// The bell of the top bar: new uploads of the subscriptions, with the number not opened yet.

#include "app/App.hpp"
#include "app/DataFiles.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"
#include "app/util/Files.hpp"

#include <algorithm>
#include <ranges>

namespace sloptube::app {
namespace {

// Videos remembered as seen: far more than one subscriptions page holds.
constexpr std::size_t kSeenKept = 300;
// The unread count on the bell, as measured on the official app: a 19 x 16 dp pill whose left
// edge is on the bell's centre line, 12 dp above its centre.
constexpr float kBadgeWidth = 19;    // dp
constexpr float kBadgeHeight = 16;   // dp
constexpr float kBadgeRise = 12;     // dp above the bell's centre.
constexpr float kBadgeTextSize = 11; // sp
constexpr int kBadgeMax = 9;         // Above it, "9+".

// The inbox, as measured on the official app.
constexpr float kChipsRow = 48;                // dp
constexpr float kSectionRow = 36;              // dp
constexpr float kNotificationRow = 89;         // dp
constexpr float kNotificationTop = 12;         // dp above the avatar and the thumbnail.
constexpr float kNotificationAvatar = 36;      // dp
constexpr float kNotificationAvatarX = 20;     // dp
constexpr float kNotificationTextX = 72;       // dp
constexpr float kNotificationThumbWidth = 115; // dp
constexpr float kNotificationThumbRight = 30;  // dp from the right edge.
constexpr float kUnreadDot = 4;                // dp

// The relative date of a feed item's detail ("264 k vues • il y a 6 heures").
std::string notificationTime(std::string_view detail) {
    const auto separator = detail.rfind("•");
    std::string_view time =
        separator == std::string_view::npos ? detail : detail.substr(separator + 3);
    while (time.starts_with(' ')) {
        time.remove_prefix(1);
    }
    return std::string(time);
}

// The inbox section of a relative date: today, this week (up to six days), or older.
Text notificationAge(std::string_view detail) {
    std::string time = notificationTime(detail);
    std::ranges::transform(time, time.begin(),
                           [](char ch) { return ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch; });
    for (const std::string_view unit : {"second", "minute", "heure", "hour"}) {
        if (time.find(unit) != std::string::npos) {
            return Text::NotificationsToday;
        }
    }
    if (time.find("jour") != std::string::npos || time.find("day") != std::string::npos) {
        const auto digit =
            std::ranges::find_if(time, [](char ch) { return ch >= '0' && ch <= '9'; });
        const int days = digit == time.end() ? 1 : *digit - '0';
        const bool oneDigit = digit + 1 == time.end() || digit[1] < '0' || digit[1] > '9';
        if (oneDigit && days < 7) {
            return Text::NotificationsThisWeek;
        }
    }
    return Text::NotificationsOlder;
}

bool notifies(const innertube::Item& item) {
    return item.kind == innertube::ItemKind::Video || item.kind == innertube::ItemKind::Live ||
           item.kind == innertube::ItemKind::Short;
}

} // namespace

void App::loadSeenNotifications() {
    m_seenNotifications.clear();
    const auto text = readText(m_platform.dataDirectory() / kNotificationsSeenFile);
    m_notificationsBaseline = text.has_value();
    if (!text) {
        return;
    }
    for (const auto line : std::views::split(*text, '\n')) {
        if (!line.empty()) {
            m_seenNotifications.emplace(line.begin(), line.end());
        }
    }
}

void App::markNotificationsSeen() {
    const auto& items = m_tabFeeds[std::to_underlying(Tab::Subscriptions)]->page.items;
    std::string text;
    std::size_t kept = 0;
    for (const auto& item : items) {
        if (notifies(item) && kept++ < kSeenKept) {
            m_seenNotifications.insert(item.id);
            text += item.id;
            text += '\n';
        }
    }
    m_notificationsBaseline = true;
    writeText(m_platform.dataDirectory() / kNotificationsSeenFile, text);
}

int App::unreadNotifications() {
    const auto& feed = *m_tabFeeds[std::to_underlying(Tab::Subscriptions)];
    if (feed.page.items.empty()) {
        return 0;
    }
    // The first time, whatever is there counts as seen: only what comes after is new.
    if (!m_notificationsBaseline) {
        markNotificationsSeen();
        return 0;
    }
    return static_cast<int>(std::ranges::count_if(feed.page.items, [&](const auto& item) {
        return notifies(item) && !m_seenNotifications.contains(item.id);
    }));
}

void App::drawUnreadBadge(const Canvas& c, ImVec2 bell, int count) {
    if (count <= 0) {
        return;
    }
    const std::string text =
        count > kBadgeMax ? std::to_string(kBadgeMax) + '+' : std::to_string(count);
    const Canvas medium = c.with(Weight::Medium);
    const float width =
        std::max(c.dp(kBadgeWidth), medium.measure(kBadgeTextSize, text).x + c.dp(8));
    const ImVec2 min{bell.x, bell.y - c.dp(kBadgeRise) - c.dp(kBadgeHeight) * 0.5f};
    const ImVec2 max{min.x + width, min.y + c.dp(kBadgeHeight)};
    // A ring of the bar's colour separates it from the bell, as on the official app.
    c.rect({min.x - c.dp(2), min.y - c.dp(2)}, {max.x + c.dp(2), max.y + c.dp(2)},
           theme::kBackground, kBadgeHeight * 0.5f + 2);
    c.rect(min, max, theme::kBadgeRed, kBadgeHeight * 0.5f);
    medium.text({(min.x + max.x - medium.measure(kBadgeTextSize, text).x) * 0.5f,
                 (min.y + max.y - c.lineHeight(kBadgeTextSize)) * 0.5f},
                kBadgeTextSize, theme::kTextPrimary, text);
}

void App::openNotifications() {
    Screen screen{.route = Route::Notifications, .title = std::string(tr(Text::Notifications))};
    load(screen.feed, [this] { return m_api.feed(innertube::Feed::Subscriptions); });
    // What was new keeps its blue dot while the list is open; it is seen from now on.
    m_unreadAtOpen.clear();
    for (const auto& item : m_tabFeeds[std::to_underlying(Tab::Subscriptions)]->page.items) {
        if (notifies(item) && !m_seenNotifications.contains(item.id)) {
            m_unreadAtOpen.insert(item.id);
        }
    }
    markNotificationsSeen();
    push(std::move(screen));
}

void App::drawNotifications(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const Canvas bold = c.with(Weight::Bold);
    const Canvas medium = c.with(Weight::Medium);
    const float width = max.x - min.x;
    const float bar = c.dp(theme::kTopBarHeight);
    const auto& feed = screen.feed;
    const ImVec2 listMin{min.x, min.y + bar};

    if (feed->page.items.empty()) {
        if (drawFeedStatus(frame, *feed, listMin, max, ItemStyle::Row) && feed->request) {
            load(feed, feed->request);
        }
        drawTitleBar(frame, screen.title, {min.x, 0}, width);
        return;
    }

    // Rows grouped as the official inbox: today, this week, older.
    struct Row {
        std::optional<Text> section;
        std::size_t item = 0;
    };

    std::vector<Row> rows;
    std::optional<Text> current;
    for (std::size_t i = 0; i < feed->page.items.size(); ++i) {
        const auto& item = feed->page.items[i];
        if (!notifies(item)) {
            continue;
        }
        const Text section = notificationAge(item.detail);
        if (section != current) {
            rows.push_back({.section = section});
            current = section;
        }
        rows.push_back({.item = i});
    }
    const float itemHeight = c.dp(kNotificationRow);
    const float sectionHeight = c.dp(kSectionRow);
    float content = c.dp(kChipsRow);
    for (const Row& row : rows) {
        content += row.section ? sectionHeight : itemHeight;
    }
    feed->scroller.update(frame.touch, listMin, max, content, frame.seconds, c.dp(1));
    frame.animating |= feed->scroller.moving();
    if (!feed->loading && !feed->page.continuation.empty() &&
        feed->scroller.offset() + (max.y - listMin.y) * 3 > content) {
        loadMore(feed);
    }

    c.list()->PushClipRect(listMin, max, true);
    float y = listMin.y - feed->scroller.offset();
    // "All", the only kind of notification this sign-in can read.
    float chipX = min.x + c.dp(theme::kGutter);
    (void)chipButton(frame, tr(Text::NotificationsAll), {chipX, y + c.dp(8)}, true, chipX);
    y += c.dp(kChipsRow);

    const float avatar = c.dp(kNotificationAvatar);
    const float textX = min.x + c.dp(kNotificationTextX);
    const float thumbWidth = c.dp(kNotificationThumbWidth);
    const float thumbX = max.x - c.dp(kNotificationThumbRight) - thumbWidth;
    const float textWidth = thumbX - c.dp(theme::kGutter) - textX;
    for (const Row& row : rows) {
        const float height = row.section ? sectionHeight : itemHeight;
        if (y + height < listMin.y || y > max.y) {
            y += height;
            continue;
        }
        if (row.section) {
            medium.text(
                {min.x + c.dp(16), y + (sectionHeight - c.lineHeight(theme::kBodySize)) * 0.5f},
                theme::kBodySize, theme::kTextSecondary, tr(*row.section));
            y += height;
            continue;
        }
        const auto& item = feed->page.items[row.item];
        const ImVec2 rowMin{min.x, y};
        const ImVec2 rowMax{max.x, y + height};
        drawPressed(frame, rowMin, rowMax);
        const float top = y + c.dp(kNotificationTop);
        if (m_unreadAtOpen.contains(item.id)) {
            c.list()->AddCircleFilled({min.x + c.dp(8), top + avatar * 0.5f + c.dp(2)},
                                      c.dp(kUnreadDot) * 0.5f, theme::kLink);
        }
        drawAvatar(frame, itemAvatar(frame, item), item.channel,
                   {min.x + c.dp(kNotificationAvatarX) + avatar * 0.5f, top + avatar * 0.5f},
                   kNotificationAvatar);
        float textY = top;
        bold.line({textX, textY}, textWidth, theme::kBodySize, theme::kTextPrimary, item.channel);
        textY += c.lineHeight(theme::kBodySize);
        textY += c.paragraph({textX, textY}, textWidth, theme::kBodySize, theme::kTextPrimary,
                             std::string(tr(Text::UploadedPrefix)) + item.title, 2);
        c.line({textX, textY}, textWidth, theme::kCaptionSize, theme::kTextSecondary,
               notificationTime(item.detail));
        const ImVec2 thumbMin{thumbX, top};
        const ImVec2 thumbMax{thumbX + thumbWidth, top + thumbWidth * theme::kAspect16x9};
        drawThumbnail(frame, imageUrl(item), thumbMin, thumbMax, theme::kCornerRadius);
        const ImVec2 more{max.x - c.dp(16), top + c.dp(4)};
        c.icon(Icon::MoreHorizontal, more, 18, theme::kTextPrimary);
        if (!feed->scroller.moving()) {
            if (frame.touch.tapped({thumbMax.x, y}, rowMax)) {
                later([this, item] { openItemActions(item); });
            } else if (frame.touch.tapped(rowMin, rowMax)) {
                later([this, item, source = VideoSource{.feed = feed, .index = row.item}] {
                    openItem(item, source);
                });
            }
        }
        y += height;
    }
    c.list()->PopClipRect();
    drawTitleBar(frame, screen.title, {min.x, 0}, width);
}

} // namespace sloptube::app
