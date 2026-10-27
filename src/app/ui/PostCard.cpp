#include "app/ui/PostCard.hpp"

#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"
#include "app/ui/Touch.hpp"

#include <algorithm>
#include <format>

namespace sloptube::app {
namespace {

// Measured on the official app.
constexpr float kPadding = 12; // dp around the header and between blocks.
constexpr float kNameGap = 2;  // dp between the author and the date.
constexpr float kTextSize = theme::kTitleSize;
constexpr int kClampedLines = 3;
constexpr int kAllLines = 1000;
constexpr float kCarouselPeek = 24;      // dp of the next image showing at the right edge.
constexpr float kMinImageAspect = 0.5f;  // Height over width limits of a single image, which is
constexpr float kMaxImageAspect = 1.25f; // centre-cropped beyond them.
constexpr float kChoiceHeight = 44;      // dp, poll and quiz answers.
constexpr float kChoiceGap = 8;          // dp
constexpr float kChoiceRadius = 4;       // dp
constexpr float kBorder = 1;             // dp
constexpr float kVideoGap = 8;           // dp between an attached video's thumbnail and title.
constexpr int kVideoTitleLines = 2;
constexpr float kActionsHeight = 48; // dp
constexpr float kActionGap = 8;      // dp between an action icon and its count.
constexpr float kCounterMargin = 10; // dp, carousel page counter from the image corner.
constexpr float kCounterPadding = 8; // dp
constexpr ImU32 kChoiceBorder = theme::kSurfacePressed;
constexpr std::string_view kSeparator = " • ";

// Vertical positions inside a card, relative to its top.
struct Layout {
    float header = 0; // Height of the avatar and name block.
    float textTop = 0;
    int textLines = 0;
    bool truncated = false; // Clamped text followed by "Read more".
    float attachmentTop = 0;
    float attachment = 0; // Height of the image, carousel, poll or video block.
    float tileWidth = 0;  // Image or carousel page width.
    float actionsTop = 0;
    float height = 0;
};

bool clamped(const PostCard& card, PostLayout layout) {
    return layout == PostLayout::Feed && !card.expanded;
}

Layout layoutOf(const Canvas& c, const PostCard& card, float width, PostLayout layout) {
    const auto& post = card.post;
    const float gutter = c.dp(theme::kGutter);
    const float padding = c.dp(kPadding);
    const float inner = width - gutter * 2;
    Layout result;
    float y = padding;
    result.header =
        std::max(c.dp(theme::kAvatarSize), c.lineHeight(theme::kBodySize) * 2 + c.dp(kNameGap));
    y += result.header + padding;

    if (!post.text.empty()) {
        result.textTop = y;
        if (clamped(card, layout)) {
            const int lines = c.lineCount(inner, kTextSize, post.text, kClampedLines + 1);
            result.truncated = lines > kClampedLines;
            result.textLines = std::min(lines, kClampedLines);
        } else {
            result.textLines = c.lineCount(inner, kTextSize, post.text, kAllLines);
        }
        y += static_cast<float>(result.textLines) * c.lineHeight(kTextSize) +
             (result.truncated ? c.lineHeight(theme::kBodySize) : 0) + padding;
    }

    result.attachmentTop = y;
    if (post.images.size() == 1) {
        const auto& image = post.images.front();
        const float aspect =
            image.width > 0 && image.height > 0
                ? static_cast<float>(image.height) / static_cast<float>(image.width)
                : 1;
        result.tileWidth = inner;
        result.attachment = inner * std::clamp(aspect, kMinImageAspect, kMaxImageAspect);
    } else if (post.images.size() > 1) {
        result.tileWidth = inner - c.dp(kCarouselPeek);
        result.attachment = result.tileWidth; // Carousel pages are square crops.
    } else if (!post.choices.empty()) {
        const auto count = static_cast<float>(post.choices.size());
        result.attachment = count * c.dp(kChoiceHeight) + (count - 1) * c.dp(kChoiceGap);
        if (!post.votes.empty()) {
            result.attachment += c.dp(kChoiceGap) + c.lineHeight(theme::kCaptionSize);
        }
    } else if (post.video) {
        result.attachment = inner * theme::kAspect16x9 + c.dp(kVideoGap) +
                            c.lineHeight(theme::kBodySize) * kVideoTitleLines +
                            c.lineHeight(theme::kCaptionSize);
    }
    y += result.attachment;

    result.actionsTop = y;
    y += c.dp(kActionsHeight);
    result.height = y + padding;
    return result;
}

// Taps clipped to the current clip rectangle, so rows scrolled under a header stay inert.
bool tappedIn(Frame& frame, ImVec2 min, ImVec2 max) {
    const ImVec2 clipMin = frame.canvas.list()->GetClipRectMin();
    const ImVec2 clipMax = frame.canvas.list()->GetClipRectMax();
    min = {std::max(min.x, clipMin.x), std::max(min.y, clipMin.y)};
    max = {std::min(max.x, clipMax.x), std::min(max.y, clipMax.y)};
    return min.x < max.x && min.y < max.y && frame.touch.tapped(min, max);
}

// Pages of a multi-image post, scrolled sideways, each with its "n/total" counter.
void drawCarousel(Frame& frame, PostCard& card, ImVec2 min, float width, const Layout& layout) {
    const Canvas& c = frame.canvas;
    const float gutter = c.dp(theme::kGutter);
    const float tile = layout.tileWidth;
    const float stride = tile + gutter;
    const ImVec2 max{min.x + width, min.y + layout.attachment};
    const auto& images = card.post.images;
    card.carousel.update(frame.touch, min, max, gutter + static_cast<float>(images.size()) * stride,
                         frame.seconds, c.dp(1));
    frame.animating |= card.carousel.moving();

    c.list()->PushClipRect(min, max, true);
    for (std::size_t index = 0; index < images.size(); ++index) {
        const float x =
            min.x + gutter + static_cast<float>(index) * stride - card.carousel.offset();
        if (x + tile < min.x) {
            continue;
        }
        if (x >= max.x) {
            break;
        }
        drawThumbnail(frame, images[index].url, {x, min.y}, {x + tile, max.y}, theme::kCardRadius);
        const std::string counter = std::format("{}/{}", index + 1, images.size());
        const ImVec2 extent = c.measure(theme::kCaptionSize, counter);
        const float padding = c.dp(kCounterPadding);
        const ImVec2 pillMax{x + tile - c.dp(kCounterMargin),
                             min.y + c.dp(kCounterMargin) + extent.y + padding * 0.5f};
        const ImVec2 pillMin{pillMax.x - extent.x - padding * 2, min.y + c.dp(kCounterMargin)};
        c.rect(pillMin, pillMax, theme::kBadge, theme::kPillRadius);
        c.text({pillMin.x + padding, pillMin.y + padding * 0.25f}, theme::kCaptionSize,
               theme::kTextPrimary, counter);
    }
    c.list()->PopClipRect();
}

void drawChoices(const Canvas& c, const innertube::Post& post, ImVec2 min, float width) {
    const float padding = c.dp(theme::kGutter);
    const float height = c.dp(kChoiceHeight);
    float y = min.y;
    for (const auto& choice : post.choices) {
        c.list()->AddRect({min.x, y}, {min.x + width, y + height}, kChoiceBorder,
                          c.dp(kChoiceRadius), c.dp(kBorder));
        c.line({min.x + padding, y + (height - c.lineHeight(theme::kBodySize)) * 0.5f},
               width - padding * 2, theme::kBodySize, theme::kTextPrimary, choice);
        y += height + c.dp(kChoiceGap);
    }
    if (!post.votes.empty()) {
        c.text({min.x, y}, theme::kCaptionSize, theme::kTextSecondary, post.votes);
    }
}

// Returns whether the video was tapped.
bool drawVideo(Frame& frame, const innertube::Item& video, ImVec2 min, float width) {
    const Canvas& c = frame.canvas;
    const ImVec2 thumbnailMax{min.x + width, min.y + width * theme::kAspect16x9};
    drawThumbnail(frame, imageUrl(video), min, thumbnailMax, theme::kCardRadius);
    drawDurationBadge(frame, video.badge, thumbnailMax);
    float y = thumbnailMax.y + c.dp(kVideoGap);
    const float titleHeight = c.lineHeight(theme::kBodySize) * kVideoTitleLines;
    c.with(theme::kTitleWeight)
        .paragraph({min.x, y}, width, theme::kBodySize, theme::kTextPrimary, video.title,
                   kVideoTitleLines);
    y += titleHeight;
    c.with(theme::kLabelWeight)
        .line({min.x, y}, width, theme::kCaptionSize, theme::kTextSecondary,
              video.channel.empty() ? video.detail
                                    : video.channel + std::string(kSeparator) + video.detail);
    return tappedIn(frame, min, {min.x + width, y + c.lineHeight(theme::kCaptionSize)});
}

} // namespace

float postCardHeight(const Canvas& canvas, PostCard& card, float width, PostLayout layout) {
    const bool clamp = clamped(card, layout);
    if (card.layoutWidth != width || card.layoutClamped != clamp) {
        card.height = layoutOf(canvas, card, width, layout).height;
        card.layoutWidth = width;
        card.layoutClamped = clamp;
    }
    return card.height;
}

PostAction drawPostCard(Frame& frame, PostCard& card, ImVec2 origin, float width,
                        PostLayout layout) {
    const Canvas& c = frame.canvas;
    const Canvas medium = c.with(Weight::Medium);
    const auto& post = card.post;
    const Layout l = layoutOf(c, card, width, layout);
    const float gutter = c.dp(theme::kGutter);
    const float inner = width - gutter * 2;
    const ImVec2 cardMax{origin.x + width, origin.y + l.height};
    if (layout == PostLayout::Feed) {
        drawPressed(frame, origin, cardMax);
    }
    PostAction action = PostAction::None;

    // Header: avatar, author and date.
    const float avatar = c.dp(theme::kAvatarSize);
    const float headerTop = origin.y + c.dp(kPadding);
    drawAvatar(frame, post.avatar, post.author,
               {origin.x + gutter + avatar * 0.5f, headerTop + l.header * 0.5f},
               theme::kAvatarSize);
    const float nameX = origin.x + gutter * 2 + avatar;
    const float nameWidth = origin.x + width - gutter - nameX;
    const float namesTop =
        headerTop + (l.header - c.lineHeight(theme::kBodySize) * 2 - c.dp(kNameGap)) * 0.5f;
    medium.line({nameX, namesTop}, nameWidth, theme::kBodySize, theme::kTextPrimary, post.author);
    c.line({nameX, namesTop + c.lineHeight(theme::kBodySize) + c.dp(kNameGap)}, nameWidth,
           theme::kBodySize, theme::kTextSecondary, post.published);
    const float namesRight =
        nameX + std::min(nameWidth, std::max(medium.measure(theme::kBodySize, post.author).x,
                                             c.measure(theme::kBodySize, post.published).x));
    if (tappedIn(frame, {origin.x, headerTop}, {namesRight, headerTop + l.header})) {
        action = PostAction::OpenChannel;
    }

    // Text, clamped in the feed with "Read more" expanding it in place.
    if (l.textLines > 0) {
        const float top = origin.y + l.textTop;
        const float textHeight =
            c.paragraph({origin.x + gutter, top}, inner, kTextSize, theme::kTextPrimary, post.text,
                        clamped(card, layout) ? kClampedLines : kAllLines);
        if (l.truncated) {
            c.text({origin.x + gutter, top + textHeight}, theme::kBodySize, theme::kTextSecondary,
                   tr(Text::ReadMore));
            if (tappedIn(frame, {origin.x, top},
                         {cardMax.x, top + textHeight + c.lineHeight(theme::kBodySize)})) {
                card.expanded = true;
            }
        }
    }

    // Attachment.
    const ImVec2 attachment{origin.x + gutter, origin.y + l.attachmentTop};
    if (post.images.size() == 1) {
        drawThumbnail(frame, post.images.front().url, attachment,
                      {attachment.x + inner, attachment.y + l.attachment}, theme::kCardRadius);
    } else if (post.images.size() > 1) {
        drawCarousel(frame, card, {origin.x, attachment.y}, width, l);
    } else if (!post.choices.empty()) {
        drawChoices(c, post, attachment, inner);
    } else if (post.video && drawVideo(frame, *post.video, attachment, inner)) {
        action = PostAction::OpenVideo;
    }

    // Actions: like and dislike on the left, comments on the right.
    const float actionsTop = origin.y + l.actionsTop;
    const float actionsBottom = actionsTop + c.dp(kActionsHeight);
    const float centerY = actionsTop + c.dp(kActionsHeight) * 0.5f;
    const float icon = c.dp(theme::kIconSize);
    const float textY = centerY - c.lineHeight(theme::kBodySize) * 0.5f;
    float x = origin.x + gutter;
    c.icon(Icon::ThumbUp, {x + icon * 0.5f, centerY}, theme::kIconSize, theme::kTextPrimary);
    x += icon;
    if (!post.likes.empty()) {
        c.text({x + c.dp(kActionGap), textY}, theme::kBodySize, theme::kTextPrimary, post.likes);
        x += c.dp(kActionGap) + c.measure(theme::kBodySize, post.likes).x;
    }
    x += gutter * 2;
    c.icon(Icon::ThumbDown, {x + icon * 0.5f, centerY}, theme::kIconSize, theme::kTextPrimary);
    x += icon + gutter;
    if (tappedIn(frame, {origin.x, actionsTop}, {x, actionsBottom})) {
        action = PostAction::Rate;
    }

    const float countWidth = c.measure(theme::kBodySize, post.commentCount).x;
    float right = origin.x + width - gutter;
    if (countWidth > 0) {
        c.text({right - countWidth, textY}, theme::kBodySize, theme::kTextPrimary,
               post.commentCount);
        right -= countWidth + c.dp(kActionGap);
    }
    c.icon(Icon::Comment, {right - icon * 0.5f, centerY}, theme::kIconSize, theme::kTextPrimary);
    if (tappedIn(frame, {right - icon - gutter, actionsTop}, {cardMax.x, actionsBottom})) {
        action = PostAction::Open;
    }

    if (action == PostAction::None && tappedIn(frame, origin, cardMax)) {
        action = PostAction::Open;
    }
    return action;
}

} // namespace sloptube::app
