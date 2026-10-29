#include "app/ui/Views.hpp"

#include "app/ui/Avatars.hpp"
#include "app/ui/Motion.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"
#include "app/ui/Thumbnails.hpp"
#include "app/ui/Touch.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace sloptube::app {
namespace {

using innertube::Item;
using innertube::ItemKind;

constexpr float kTextGap = 4;     // dp between stacked text blocks.
constexpr float kIconTarget = 48; // dp, minimum touch target.
constexpr float kTitleLines = 2;
constexpr float kRowTitleLines = 3;
constexpr float kStatusWidth = 280; // dp, width of centred status messages.
constexpr std::string_view kSeparator = " · ";
constexpr float kRippleSpeed = 2.5f; // Ripple radius growth, in rectangle diagonals per second.
constexpr ImU32 kRipple = IM_COL32(0xff, 0xff, 0xff, 0x24);
constexpr ImU32 kShimmer = IM_COL32(0xff, 0xff, 0xff, 0x10);
constexpr int kSkeletonRows = 6;

struct CardMetrics {
    float thumbnail;
    float height;
};

CardMetrics cardMetrics(const Frame& frame, float width) {
    const Canvas& c = frame.canvas;
    const float thumbnail = width * theme::kAspect16x9;
    const float text = c.dp(theme::kGutter) + c.lineHeight(theme::kTitleSize) * kTitleLines +
                       c.dp(kTextGap) + c.lineHeight(theme::kCaptionSize);
    return {thumbnail, thumbnail + text + c.dp(theme::kCardSpacing)};
}

// A channel among search results, as measured on the official app: a large avatar beside the name,
// handle and subscribers, then a button to the channel.
constexpr float kChannelPadding = 20; // dp, above the avatar and below the button
constexpr float kChannelGap = 13;     // dp, between the avatar and the button

float channelCardHeight(const Frame& frame) {
    return frame.dp(kChannelPadding * 2 + theme::kLargeAvatarSize + kChannelGap +
                    theme::kButtonHeight);
}

float rowHeight(const Frame& frame) {
    return frame.dp(theme::kRowThumbnailWidth * theme::kAspect16x9 + theme::kGutter);
}

std::string metaLine(const Item& item) {
    if (item.detail.empty()) {
        return item.channel;
    }
    if (item.channel.empty()) {
        return item.detail;
    }
    return item.channel + std::string(kSeparator) + item.detail;
}

void drawBadge(Frame& frame, std::string_view text, ImVec2 thumbnailMax) {
    if (text.empty()) {
        return;
    }
    const Canvas& c = frame.canvas;
    const Canvas label = c.with(theme::kLabelWeight);
    const ImVec2 extent = label.measure(theme::kCaptionSize, text);
    const float padding = c.dp(theme::kBadgePadding);
    const ImVec2 max{thumbnailMax.x - c.dp(theme::kBadgeMargin),
                     thumbnailMax.y - c.dp(theme::kBadgeMargin)};
    const ImVec2 min{max.x - extent.x - padding * 2, max.y - extent.y - padding};
    c.rect(min, max, theme::kBadge, theme::kBadgePadding);
    label.text({min.x + padding, min.y + padding * 0.5f}, theme::kCaptionSize, theme::kTextPrimary,
               text);
}

void drawCard(Frame& frame, const Item& item, ImVec2 origin, float width) {
    const Canvas& c = frame.canvas;
    const CardMetrics metrics = cardMetrics(frame, width);
    drawPressed(frame, origin, {origin.x + width, origin.y + metrics.height});

    const ImVec2 thumbnailMax{origin.x + width, origin.y + metrics.thumbnail};
    drawThumbnail(frame, imageUrl(item), origin, thumbnailMax, 0);
    drawBadge(frame, item.badge, thumbnailMax);

    const float gutter = c.dp(theme::kGutter);
    const float avatar = c.dp(theme::kAvatarSize);
    const float top = thumbnailMax.y + gutter;
    drawAvatar(frame, itemAvatar(frame, item), item.channel,
               {origin.x + gutter + avatar * 0.5f, top + avatar * 0.5f}, theme::kAvatarSize);

    const float textX = origin.x + gutter * 2 + avatar;
    const float textWidth = origin.x + width - gutter - textX;
    const float titleHeight =
        c.with(theme::kTitleWeight)
            .paragraph({textX, top}, textWidth, theme::kTitleSize, theme::kTextPrimary, item.title,
                       static_cast<int>(kTitleLines));
    c.with(theme::kLabelWeight)
        .line({textX, top + titleHeight + c.dp(kTextGap)}, textWidth, theme::kCaptionSize,
              theme::kTextSecondary, metaLine(item));
}

// Returns whether its button was tapped (the rest of the card is hit-tested by the list).
bool drawChannelCard(Frame& frame, const Item& item, ImVec2 origin, float width) {
    const Canvas& c = frame.canvas;
    const Canvas heading = c.with(theme::kHeadingWeight);
    const float gutter = c.dp(theme::kGutter);
    const float avatar = c.dp(theme::kLargeAvatarSize);
    const float top = origin.y + c.dp(kChannelPadding);
    drawAvatar(frame, item.avatar, item.title,
               {origin.x + gutter + avatar * 0.5f, top + avatar * 0.5f}, theme::kLargeAvatarSize);

    const float textX = origin.x + gutter * 2 + avatar;
    const float textWidth = origin.x + width - gutter - textX;
    const float textHeight =
        c.lineHeight(theme::kHeadlineSize) + c.lineHeight(theme::kCaptionSize) * 2;
    float y = top + (avatar - textHeight) * 0.5f;
    heading.line({textX, y}, textWidth, theme::kHeadlineSize, theme::kTextPrimary, item.title);
    y += c.lineHeight(theme::kHeadlineSize);
    c.line({textX, y}, textWidth, theme::kCaptionSize, theme::kTextSecondary, item.channel);
    y += c.lineHeight(theme::kCaptionSize);
    c.line({textX, y}, textWidth, theme::kCaptionSize, theme::kTextSecondary, item.detail);

    const float buttonTop = top + avatar + c.dp(kChannelGap);
    return pillButton(frame, tr(Text::GoToChannel), {origin.x + gutter, buttonTop},
                      {origin.x + width - gutter, buttonTop + c.dp(theme::kButtonHeight)}, false);
}

void drawRow(Frame& frame, const Item& item, ImVec2 origin, float width) {
    const Canvas& c = frame.canvas;
    const float gutter = c.dp(theme::kGutter);
    const float height = rowHeight(frame);
    drawPressed(frame, origin, {origin.x + width, origin.y + height});

    const ImVec2 thumbnailMin{origin.x + gutter, origin.y};
    const ImVec2 thumbnailMax{thumbnailMin.x + c.dp(theme::kRowThumbnailWidth),
                              thumbnailMin.y +
                                  c.dp(theme::kRowThumbnailWidth) * theme::kAspect16x9};
    if (item.kind == ItemKind::Channel) {
        drawAvatar(
            frame, item.avatar, item.title,
            {(thumbnailMin.x + thumbnailMax.x) * 0.5f, (thumbnailMin.y + thumbnailMax.y) * 0.5f},
            theme::kLargeAvatarSize);
    } else {
        drawThumbnail(frame, imageUrl(item), thumbnailMin, thumbnailMax, theme::kCornerRadius);
        drawBadge(frame, item.badge, thumbnailMax);
    }

    const float textX = thumbnailMax.x + gutter;
    const float textWidth = origin.x + width - gutter - textX;
    float y = origin.y;
    y += c.paragraph({textX, y}, textWidth, theme::kBodySize, theme::kTextPrimary, item.title,
                     static_cast<int>(kRowTitleLines - 1));
    y += c.dp(kTextGap);
    c.line({textX, y}, textWidth, theme::kCaptionSize, theme::kTextSecondary, item.channel);
    y += c.lineHeight(theme::kCaptionSize);
    c.line({textX, y}, textWidth, theme::kCaptionSize, theme::kTextSecondary, item.detail);
}

} // namespace

void drawDurationBadge(Frame& frame, std::string_view text, ImVec2 thumbnailMax) {
    drawBadge(frame, text, thumbnailMax);
}

std::string imageUrl(const Item& item) {
    switch (item.kind) {
    case ItemKind::Video:
    case ItemKind::Short:
    case ItemKind::Live:
        return thumbnailUrl(item.id);
    case ItemKind::Channel:
    case ItemKind::Playlist:
        break;
    }
    return item.thumbnail;
}

std::string formatDuration(std::int64_t seconds) {
    seconds = std::max<std::int64_t>(seconds, 0);
    const auto hours = seconds / 3600;
    const auto minutes = (seconds / 60) % 60;
    const auto rest = seconds % 60;
    if (hours > 0) {
        return std::format("{}:{:02}:{:02}", hours, minutes, rest);
    }
    return std::format("{}:{:02}", minutes, rest);
}

void drawThumbnail(Frame& frame, const std::string& url, ImVec2 min, ImVec2 max, float roundingDp,
                   int maxWidth) {
    const Canvas& c = frame.canvas;
    const Texture* texture = frame.thumbnails.get(url, maxWidth);
    const float fade = texture == nullptr
                           ? 0
                           : std::chrono::duration<float>(frame.now - texture->readyAt).count() /
                                 motion::kFadeDuration;
    if (fade < 1) {
        c.rect(min, max, theme::kPlaceholder, roundingDp);
    }
    if (texture == nullptr) {
        return;
    }
    if (fade < 1) {
        frame.animating = true;
    }
    // Centre-crop the image to the box's aspect ratio (strips 4:3 letterboxing).
    const float boxAspect = (max.x - min.x) / std::max(max.y - min.y, 1.0f);
    const float imageAspect =
        static_cast<float>(texture->width) / static_cast<float>(std::max(texture->height, 1));
    ImVec2 uv0{0, 0};
    ImVec2 uv1{1, 1};
    if (imageAspect < boxAspect) {
        const float visible = imageAspect / boxAspect;
        uv0.y = (1 - visible) * 0.5f;
        uv1.y = 1 - uv0.y;
    } else {
        const float visible = boxAspect / imageAspect;
        uv0.x = (1 - visible) * 0.5f;
        uv1.x = 1 - uv0.x;
    }
    c.list()->AddImageRounded(texture->ref(), min, max, uv0, uv1,
                              motion::withAlpha(IM_COL32_WHITE, motion::easeOutCubic(fade)),
                              c.dp(roundingDp));
}

void drawAvatar(Frame& frame, const std::string& url, std::string_view name, ImVec2 center,
                float sizeDp) {
    const Canvas& c = frame.canvas;
    const float radius = c.dp(sizeDp) * 0.5f;
    const int pixels = static_cast<int>(radius * 2);
    const Texture* texture = url.empty() ? nullptr : frame.thumbnails.get(avatarUrl(url, pixels));
    if (texture == nullptr) {
        c.avatar(center, sizeDp, name);
        return;
    }
    const float fade =
        std::chrono::duration<float>(frame.now - texture->readyAt).count() / motion::kFadeDuration;
    if (fade < 1) {
        c.avatar(center, sizeDp, name);
        frame.animating = true;
    }
    c.list()->AddImageRounded(texture->ref(), {center.x - radius, center.y - radius},
                              {center.x + radius, center.y + radius}, {0, 0}, {1, 1},
                              motion::withAlpha(IM_COL32_WHITE, motion::easeOutCubic(fade)),
                              radius);
}

const std::string& itemAvatar(Frame& frame, const Item& item) {
    return item.avatar.empty() ? frame.avatars.get(item) : item.avatar;
}

void drawPressed(Frame& frame, ImVec2 min, ImVec2 max, float roundingDp) {
    if (!frame.touch.pressed(min, max)) {
        return;
    }
    const Canvas& c = frame.canvas;
    const float diagonal = std::hypot(max.x - min.x, max.y - min.y);
    const float t = frame.touch.pressDuration() * kRippleSpeed;
    c.rect(min, max, motion::withAlpha(theme::kSurfacePressed, std::min(t * 2, 1.0f) * 0.5f),
           roundingDp);
    c.list()->PushClipRect(min, max, true);
    c.list()->AddCircleFilled(frame.touch.start(), diagonal * motion::easeOutCubic(t), kRipple);
    c.list()->PopClipRect();
    frame.animating = true;
}

void drawSkeleton(Frame& frame, ItemStyle style, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const float width = max.x - min.x;
    const float gutter = c.dp(theme::kGutter);
    const float line = c.lineHeight(theme::kBodySize) * 0.7f;
    ImDrawList* list = c.list();
    list->PushClipRect(min, max, true);

    float y = min.y;
    for (int row = 0; row < kSkeletonRows && y < max.y; ++row) {
        if (style == ItemStyle::Card) {
            const float thumbnail = width * theme::kAspect16x9;
            c.rect({min.x, y}, {max.x, y + thumbnail}, theme::kPlaceholder);
            const float top = y + thumbnail + gutter;
            const float avatar = c.dp(theme::kAvatarSize);
            list->AddCircleFilled({min.x + gutter + avatar * 0.5f, top + avatar * 0.5f},
                                  avatar * 0.5f, theme::kPlaceholder);
            const float textX = min.x + gutter * 2 + avatar;
            c.rect({textX, top}, {max.x - gutter * 2, top + line}, theme::kPlaceholder, 4);
            c.rect({textX, top + line * 1.6f}, {textX + (max.x - textX) * 0.55f, top + line * 2.6f},
                   theme::kPlaceholder, 4);
            y += cardMetrics(frame, width).height;
        } else {
            const float thumbWidth = c.dp(theme::kRowThumbnailWidth);
            c.rect({min.x + gutter, y},
                   {min.x + gutter + thumbWidth, y + thumbWidth * theme::kAspect16x9},
                   theme::kPlaceholder, theme::kCornerRadius);
            const float textX = min.x + gutter * 2 + thumbWidth;
            c.rect({textX, y}, {max.x - gutter, y + line}, theme::kPlaceholder, 4);
            c.rect({textX, y + line * 1.6f}, {textX + (max.x - textX) * 0.5f, y + line * 2.6f},
                   theme::kPlaceholder, 4);
            y += rowHeight(frame);
        }
    }

    // A soft diagonal band sweeping across every placeholder.
    const float phase =
        static_cast<float>(std::fmod(frame.time, motion::kShimmerPeriod)) / motion::kShimmerPeriod;
    const float band = width * 0.4f;
    const float x = min.x - band + (width + band * 2) * phase;
    list->AddRectFilledMultiColor({x - band, min.y}, {x, max.y}, IM_COL32(0, 0, 0, 0), kShimmer,
                                  kShimmer, IM_COL32(0, 0, 0, 0));
    list->AddRectFilledMultiColor({x, min.y}, {x + band, max.y}, kShimmer, IM_COL32(0, 0, 0, 0),
                                  IM_COL32(0, 0, 0, 0), kShimmer);
    list->PopClipRect();
    frame.animating = true;
}

std::optional<std::size_t> drawList(Frame& frame, Feed& feed, ItemStyle style, ImVec2 min,
                                    ImVec2 max, const ListHeader& header,
                                    const std::function<void()>& nearEnd,
                                    std::span<const ListInsert> inserts) {
    const float width = max.x - min.x;
    const float viewport = max.y - min.y;
    const float cardHeight =
        style == ItemStyle::Card ? cardMetrics(frame, width).height : rowHeight(frame);
    const float channelHeight = style == ItemStyle::Card ? channelCardHeight(frame) : cardHeight;
    const auto itemHeight = [&](const Item& item) {
        return item.kind == ItemKind::Channel ? channelHeight : cardHeight;
    };
    const bool more = feed.loading || !feed.page.continuation.empty();
    const float footer = more ? frame.dp(kIconTarget) : 0;
    const auto count = feed.page.items.size();
    // Inserts only count once the items above them exist.
    while (!inserts.empty() && inserts.back().after > count) {
        inserts = inserts.first(inserts.size() - 1);
    }
    float insertsHeight = 0;
    for (const auto& insert : inserts) {
        insertsHeight += insert.height;
    }
    float itemsHeight = 0;
    for (const Item& item : feed.page.items) {
        itemsHeight += itemHeight(item);
    }
    const float content = header.height + itemsHeight + insertsHeight + footer;

    feed.scroller.update(frame.touch, min, max, content, frame.seconds, frame.dp(1));
    frame.animating |= feed.scroller.moving();
    const float offset = feed.scroller.offset();

    ImDrawList* list = frame.canvas.list();
    list->PushClipRect(min, max, true);

    if (header.draw && offset < header.height) {
        header.draw(frame, {min.x, min.y - offset}, width);
    }

    // Items and inserts are laid out top to bottom; an insert sits below `after` items.
    float y = min.y + header.height - offset;
    std::size_t nextInsert = 0;
    const auto drawInsertsBefore = [&](std::size_t index) {
        for (; nextInsert < inserts.size() && inserts[nextInsert].after <= index; ++nextInsert) {
            const auto& insert = inserts[nextInsert];
            if (y < max.y && y + insert.height > min.y) {
                insert.draw(frame, {min.x, y}, width);
            }
            y += insert.height;
        }
    };

    std::optional<std::size_t> tapped;
    for (std::size_t index = 0; index < count && y < max.y; ++index) {
        drawInsertsBefore(index);
        const Item& item = feed.page.items[index];
        const float height = itemHeight(item);
        const ImVec2 origin{min.x, y};
        y += height;
        if (origin.y >= max.y || y <= min.y) {
            continue;
        }
        if (item.kind == ItemKind::Channel && style == ItemStyle::Card) {
            if (drawChannelCard(frame, item, origin, width)) {
                tapped = index;
            }
        } else if (style == ItemStyle::Card) {
            drawCard(frame, item, origin, width);
        } else {
            drawRow(frame, item, origin, width);
        }
        const ImVec2 hitMin{origin.x, std::max(origin.y, min.y)};
        const ImVec2 hitMax{origin.x + width, std::min(y, max.y)};
        if (frame.touch.tapped(hitMin, hitMax)) {
            tapped = index;
        }
    }
    drawInsertsBefore(count);

    const float footerTop = min.y - offset + content - footer;
    if (more && footerTop < max.y) {
        frame.canvas.spinner({min.x + width * 0.5f, footerTop + footer * 0.5f}, theme::kSpinnerSize,
                             theme::kTextSecondary, frame.time);
        frame.animating = true;
    }
    list->PopClipRect();

    if (!feed.loading && !feed.page.continuation.empty() && offset + viewport * 2 >= content &&
        nearEnd) {
        nearEnd();
    }
    return tapped;
}

float shelfHeight(const Frame& frame) {
    const Canvas& c = frame.canvas;
    return c.dp(theme::kShelfThumbnailWidth * theme::kAspect16x9 + kTextGap * 2) +
           c.lineHeight(theme::kBodySize) * kTitleLines + c.lineHeight(theme::kCaptionSize);
}

std::optional<std::size_t> drawShelf(Frame& frame, Feed& feed, ImVec2 origin, float width) {
    const Canvas& c = frame.canvas;
    const float gutter = c.dp(theme::kGutter);
    const float tile = c.dp(theme::kShelfThumbnailWidth);
    const float stride = tile + gutter;
    const float height = shelfHeight(frame);
    const ImVec2 max{origin.x + width, origin.y + height};
    const auto count = feed.page.items.size();

    feed.scroller.update(frame.touch, origin, max, gutter + static_cast<float>(count) * stride,
                         frame.seconds, c.dp(1));
    frame.animating |= feed.scroller.moving();

    std::optional<std::size_t> tapped;
    c.list()->PushClipRect(origin, max, true);
    for (std::size_t index = 0; index < count; ++index) {
        const float x =
            origin.x + gutter + static_cast<float>(index) * stride - feed.scroller.offset();
        if (x + tile < origin.x) {
            continue;
        }
        if (x >= max.x) {
            break;
        }
        const Item& item = feed.page.items[index];
        const ImVec2 thumbnailMax{x + tile, origin.y + tile * theme::kAspect16x9};
        drawThumbnail(frame, imageUrl(item), {x, origin.y}, thumbnailMax, theme::kCornerRadius);
        drawBadge(frame, item.badge, thumbnailMax);
        float y = thumbnailMax.y + c.dp(kTextGap);
        y += c.with(theme::kTitleWeight)
                 .paragraph({x, y}, tile, theme::kBodySize, theme::kTextPrimary, item.title,
                            static_cast<int>(kTitleLines));
        c.with(theme::kLabelWeight)
            .line({x, y}, tile, theme::kCaptionSize, theme::kTextSecondary, item.channel);
        if (frame.touch.tapped({x, origin.y}, {x + tile, max.y})) {
            tapped = index;
        }
    }
    c.list()->PopClipRect();
    return tapped;
}

bool iconButton(Frame& frame, Icon icon, ImVec2 center, ImU32 color) {
    const float half = frame.dp(kIconTarget) * 0.5f;
    const ImVec2 min{center.x - half, center.y - half};
    const ImVec2 max{center.x + half, center.y + half};
    if (frame.touch.pressed(min, max)) {
        const float grow = motion::easeOutCubic(frame.touch.pressDuration() * 6);
        frame.canvas.list()->AddCircleFilled(center, half * 0.8f * grow, theme::kSurfacePressed);
        frame.animating = true;
    }
    frame.canvas.icon(icon, center, theme::kIconSize, color);
    return frame.touch.tapped(min, max);
}

bool pillButton(Frame& frame, std::string_view label, ImVec2 min, ImVec2 max, bool primary) {
    const Canvas& c = frame.canvas;
    const bool pressed = frame.touch.pressed(min, max);
    const ImU32 fill = primary ? theme::kTextPrimary : theme::kSurface;
    motion::Layer layer(c.list());
    c.rect(min, max, fill, theme::kPillRadius);
    drawPressed(frame, min, max, theme::kPillRadius);
    const Canvas text = c.with(theme::kLabelWeight);
    const ImVec2 extent = text.measure(theme::kBodySize, label);
    text.text({(min.x + max.x - extent.x) * 0.5f, (min.y + max.y - extent.y) * 0.5f},
              theme::kBodySize, primary ? theme::kTextOnLight : theme::kTextPrimary, label);
    layer.end({}, 1, pressed ? motion::kPressScale : 1,
              {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f});
    return frame.touch.tapped(min, max);
}

bool textRow(Frame& frame, std::string_view label, ImVec2 origin, float width) {
    const Canvas& c = frame.canvas;
    const Canvas heading = c.with(theme::kHeadingWeight);
    const float height = frame.dp(kIconTarget);
    const ImVec2 max{origin.x + width, origin.y + height};
    drawPressed(frame, origin, max);
    const float gutter = c.dp(theme::kGutter);
    const ImVec2 extent = heading.measure(theme::kSectionSize, label);
    heading.text({origin.x + gutter, origin.y + (height - extent.y) * 0.5f}, theme::kSectionSize,
                 theme::kTextPrimary, label);
    c.icon(Icon::ChevronRight,
           {origin.x + gutter + extent.x + c.dp(theme::kIconSize) * 0.6f, origin.y + height * 0.5f},
           theme::kIconSize * 0.8f, theme::kTextPrimary);
    return frame.touch.tapped(origin, max);
}

bool chipButton(Frame& frame, std::string_view label, ImVec2 origin, bool selected, float& x) {
    const Canvas& c = frame.canvas;
    const Canvas medium = c.with(Weight::Medium);
    const float gutter = c.dp(theme::kGutter);
    const float height = c.dp(theme::kChipHeight);
    const float width = medium.measure(theme::kBodySize, label).x + gutter * 2;
    const ImVec2 max{origin.x + width, origin.y + height};
    c.rect(origin, max, selected ? theme::kTextPrimary : theme::kSurface, theme::kCornerRadius);
    drawPressed(frame, origin, max, theme::kCornerRadius);
    medium.text({origin.x + gutter, origin.y + (height - c.lineHeight(theme::kBodySize)) * 0.5f},
                theme::kBodySize, selected ? theme::kTextOnLight : theme::kTextPrimary, label);
    x += width + gutter * 0.75f;
    return frame.touch.tapped(origin, max);
}

bool switchRow(Frame& frame, std::string_view label, std::string_view detail, ImVec2 origin,
               float width, std::optional<bool> on) {
    constexpr float kTrackWidth = 36; // dp, as Material switches.
    constexpr float kTrackHeight = 20;
    constexpr float kKnob = 7;
    const Canvas& c = frame.canvas;
    const float gutter = c.dp(theme::kGutter);
    const float height = c.dp(kSettingRowHeight);
    const ImVec2 max{origin.x + width, origin.y + height};
    drawPressed(frame, origin, max);

    const float trackWidth = on ? c.dp(kTrackWidth) + gutter : 0;
    const float textWidth = width - gutter * 2 - trackWidth;
    const float textHeight = c.lineHeight(theme::kBodySize) + c.lineHeight(theme::kCaptionSize);
    const float textTop = origin.y + (height - textHeight) * 0.5f;
    c.line({origin.x + gutter, textTop}, textWidth, theme::kBodySize, theme::kTextPrimary, label);
    c.line({origin.x + gutter, textTop + c.lineHeight(theme::kBodySize)}, textWidth,
           theme::kCaptionSize, theme::kTextSecondary, detail);

    if (on) {
        const float centerY = origin.y + height * 0.5f;
        const ImVec2 trackMin{max.x - gutter - c.dp(kTrackWidth),
                              centerY - c.dp(kTrackHeight) * 0.5f};
        const ImVec2 trackMax{max.x - gutter, centerY + c.dp(kTrackHeight) * 0.5f};
        c.rect(trackMin, trackMax, *on ? theme::kAccent : theme::kTrack, kTrackHeight * 0.5f);
        const float knobX =
            *on ? trackMax.x - c.dp(kTrackHeight) * 0.5f : trackMin.x + c.dp(kTrackHeight) * 0.5f;
        c.list()->AddCircleFilled({knobX, centerY}, c.dp(kKnob), theme::kTextPrimary);
    }
    return frame.touch.tapped(origin, max);
}

bool drawFeedStatus(Frame& frame, const Feed& feed, ImVec2 min, ImVec2 max, ItemStyle style) {
    const Canvas& c = frame.canvas;
    const ImVec2 center{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
    if (feed.loading && feed.page.items.empty()) {
        drawSkeleton(frame, style, min, max);
        return false;
    }
    if (feed.error.empty() && (!feed.loaded || !feed.page.items.empty())) {
        return false;
    }

    const float width = std::min(c.dp(kStatusWidth), max.x - min.x);
    const std::string_view message = feed.error.empty() ? tr(Text::Nothing) : feed.error;
    const float textHeight =
        c.paragraph({center.x - width * 0.5f, center.y - c.dp(kIconTarget)}, width,
                    theme::kBodySize, theme::kTextSecondary, message, 4);
    if (feed.error.empty()) {
        return false;
    }
    const float buttonWidth = c.measure(theme::kBodySize, tr(Text::Retry)).x + c.dp(32);
    const ImVec2 buttonMin{center.x - buttonWidth * 0.5f,
                           center.y - c.dp(kIconTarget) + textHeight + c.dp(theme::kGutter)};
    return pillButton(frame, tr(Text::Retry), buttonMin,
                      {buttonMin.x + buttonWidth, buttonMin.y + c.dp(theme::kButtonHeight)}, true);
}

} // namespace sloptube::app
