// Watch page details below the player: metadata, channel, rating, comments and description.

#include "app/App.hpp"
#include "app/ui/Motion.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"
#include "app/ui/Touch.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <format>

namespace sloptube::app {
namespace {

using innertube::Comment;
using innertube::Rating;
using innertube::toggled;

constexpr std::string_view kMetaGap = "  ";
constexpr std::string_view kBullet = " • ";
constexpr float kChannelRowHeight = 48;  // dp
constexpr float kActionHeight = 36;      // dp, comment action row and rating pill.
constexpr float kActionIcon = 18;        // dp
constexpr float kActionGap = 8;          // dp between the action row's pills.
constexpr float kRowHeight = 48;         // dp, header and input rows of sheets.
constexpr float kEstimatedComment = 120; // dp, height assumed for rows not yet drawn.
constexpr int kCollapsedCommentLines = 4;
constexpr int kExpandedCommentLines = 400;
constexpr int kDescriptionLines = 1000;
// Ids of comments shown optimistically before the server has assigned them one.
constexpr std::string_view kLocalCommentPrefix = "local:";
constexpr float kPopFrom = 0.6f; // Scale the rating icon pops from.

bool tappedWithin(Frame& frame, ImVec2 min, ImVec2 max, ImVec2 clipMin, ImVec2 clipMax) {
    return frame.touch.tapped({std::max(min.x, clipMin.x), std::max(min.y, clipMin.y)},
                              {std::min(max.x, clipMax.x), std::min(max.y, clipMax.y)});
}

std::string replyLabel(std::int64_t count) {
    if (count == 1) {
        return std::string(tr(Text::OneReply));
    }
    return std::vformat(tr(Text::ReplyCount), std::make_format_args(count));
}

} // namespace

// --- Watch header --------------------------------------------------------------------------------

float App::drawWatchHeader(Frame& frame, ImVec2 origin, float width) {
    const Canvas& c = frame.canvas;
    const Canvas medium = c.with(Weight::Medium);
    const float gutter = c.dp(theme::kGutter);
    const float textWidth = width - gutter * 2;
    innertube::Watch* info = m_watch.info ? &*m_watch.info : nullptr;
    const std::string& title =
        info ? info->title : (m_watch.details ? m_watch.details->title : m_watch.item.title);

    float y = origin.y + gutter;
    const float top = y;
    y += c.with(theme::kHeadingWeight)
             .paragraph({origin.x + gutter, y}, textWidth, theme::kWatchTitleSize,
                        theme::kTextPrimary, title, 2);
    y += c.dp(4);
    std::string meta =
        info ? info->views + std::string(kMetaGap) + info->date : m_watch.item.detail;
    if (!m_watch.error.empty()) {
        meta = m_watch.error;
    }
    const float moreWidth = medium.measure(theme::kCaptionSize, tr(Text::More)).x;
    c.line({origin.x + gutter, y}, textWidth - moreWidth, theme::kCaptionSize,
           theme::kTextSecondary, meta);
    if (info) {
        medium.text({origin.x + width - gutter - moreWidth, y}, theme::kCaptionSize,
                    theme::kTextPrimary, tr(Text::More));
    }
    y += c.lineHeight(theme::kCaptionSize) + gutter;
    if (info && frame.touch.tapped({origin.x, top}, {origin.x + width, y})) {
        openSheet(Sheet::Description);
    }

    // Channel row.
    if (info) {
        drawChannelRow(frame, info->channel, {origin.x, y}, width);
    } else {
        const float avatar = c.dp(theme::kAvatarSize);
        const std::string& channel = m_watch.item.channel;
        drawAvatar(frame, itemAvatar(frame, m_watch.item), channel,
                   {origin.x + gutter + avatar * 0.5f, y + c.dp(kChannelRowHeight) * 0.5f},
                   theme::kAvatarSize);
        medium.line({origin.x + gutter * 2 + avatar,
                     y + (c.dp(kChannelRowHeight) - c.lineHeight(theme::kBodySize)) * 0.5f},
                    textWidth - avatar - gutter, theme::kBodySize, theme::kTextPrimary, channel);
    }
    y += c.dp(kChannelRowHeight) + gutter * 0.5f;

    // Action row, scrolling sideways like the official one: rating pill, Share, Download, Save.
    // Everything but the rating works before (or without) the page's details.
    {
        const float height = c.dp(kActionHeight);
        const float icon = c.dp(kActionIcon);
        const float padding = gutter;
        const ImVec2 rowMin{origin.x, y};
        const ImVec2 rowMax{origin.x + width, y + height};
        auto& scroller = m_watch.actionsScroller;
        scroller.update(frame.touch, rowMin, rowMax, m_watch.actionsWidth, frame.seconds, c.dp(1));
        frame.animating |= scroller.moving();
        c.list()->PushClipRect(rowMin, rowMax, true);
        const float start = origin.x - scroller.offset();
        float x = start + gutter;

        // Rating pill: [like count | dislike].
        if (info && info->likesAllowed) {
            const float likesWidth = c.measure(theme::kBodySize, info->likes).x;
            const float likeWidth =
                padding + icon + (likesWidth > 0 ? gutter * 0.6f + likesWidth : 0) + padding;
            const float dislikeWidth = padding * 2 + icon;
            const ImVec2 pillMin{x, y};
            const ImVec2 split{pillMin.x + likeWidth, y + height};
            const ImVec2 pillMax{split.x + dislikeWidth, y + height};
            c.rect(pillMin, pillMax, theme::kSurface, theme::kPillRadius);
            drawPressed(frame, pillMin, split, theme::kPillRadius);
            drawPressed(frame, {split.x, y}, pillMax, theme::kPillRadius);
            c.list()->AddLine({split.x, y + height * 0.25f}, {split.x, y + height * 0.75f},
                              theme::kDivider, c.dp(1));

            const float pop =
                m_watch.likePopAt < 0
                    ? 1
                    : kPopFrom + (1 - kPopFrom) *
                                     motion::easeOutBack(motion::progress(
                                         frame.time, m_watch.likePopAt, motion::kPopDuration));
            if (pop < 1 ||
                (m_watch.likePopAt >= 0 && frame.time - m_watch.likePopAt < motion::kPopDuration)) {
                frame.animating = true;
            }
            const bool liked = info->rating == Rating::Like;
            const bool disliked = info->rating == Rating::Dislike;
            const ImVec2 likeCenter{pillMin.x + padding + icon * 0.5f, y + height * 0.5f};
            const ImVec2 dislikeCenter{split.x + padding + icon * 0.5f, y + height * 0.5f};
            {
                motion::Layer layer(c.list());
                c.icon(liked ? Icon::ThumbUpFilled : Icon::ThumbUp, likeCenter, kActionIcon,
                       theme::kTextPrimary);
                layer.end({}, 1, liked ? pop : 1, likeCenter);
            }
            {
                motion::Layer layer(c.list());
                c.icon(disliked ? Icon::ThumbDownFilled : Icon::ThumbDown, dislikeCenter,
                       kActionIcon, theme::kTextPrimary);
                layer.end({}, 1, disliked ? pop : 1, dislikeCenter);
            }
            medium.text({likeCenter.x + icon * 0.5f + gutter * 0.6f,
                         y + (height - c.lineHeight(theme::kBodySize)) * 0.5f},
                        theme::kBodySize, theme::kTextPrimary, info->likes);

            if (frame.touch.tapped(pillMin, split)) {
                rateVideo(toggled(info->rating, Rating::Like));
            } else if (frame.touch.tapped({split.x, y}, pillMax)) {
                rateVideo(toggled(info->rating, Rating::Dislike));
            }
            x = pillMax.x + c.dp(kActionGap);
        }

        // Chips: an icon and a label in a pill.
        const auto chip = [&](Icon chipIcon, std::string_view text) {
            const float chipWidth =
                padding + icon + gutter * 0.5f + medium.measure(theme::kBodySize, text).x + padding;
            const ImVec2 chipMin{x, y};
            const ImVec2 chipMax{x + chipWidth, y + height};
            c.rect(chipMin, chipMax, theme::kSurface, theme::kPillRadius);
            drawPressed(frame, chipMin, chipMax, theme::kPillRadius);
            c.icon(chipIcon, {x + padding + icon * 0.5f, y + height * 0.5f}, kActionIcon,
                   theme::kTextPrimary);
            medium.text({x + padding + icon + gutter * 0.5f,
                         y + (height - c.lineHeight(theme::kBodySize)) * 0.5f},
                        theme::kBodySize, theme::kTextPrimary, text);
            x = chipMax.x + c.dp(kActionGap);
            return !scroller.moving() && frame.touch.tapped(chipMin, chipMax);
        };
        if (chip(Icon::Share, tr(Text::Share))) {
            shareVideo(m_watch.item);
        }
        const auto download = m_downloads->find(m_watch.item.id);
        std::string downloadLabel(tr(Text::Download));
        if (download && download->state == DownloadState::Done) {
            downloadLabel = tr(Text::Downloaded);
        } else if (download && download->state == DownloadState::Downloading) {
            const std::int64_t percent =
                download->total > 0 ? download->bytes * 100 / download->total : 0;
            downloadLabel = std::vformat(tr(Text::Percent), std::make_format_args(percent));
            frame.animating = true;
        }
        // A broadcast has no end to download up to.
        const bool live = m_watch.details && m_watch.details->live;
        if (!live && chip(download && download->state == DownloadState::Done ? Icon::Downloaded
                                                                             : Icon::Download,
                          downloadLabel)) {
            later([this] { openDownload(m_watch.item); });
        }
        if (chip(Icon::Save, tr(Text::Save))) {
            later([this, id = m_watch.item.id] { openSave(id); });
        }
        c.list()->PopClipRect();
        m_watch.actionsWidth = x - c.dp(kActionGap) + gutter - start;
        y += height + gutter;
    }

    // Comments teaser card.
    if (info && !info->commentsToken.empty()) {
        const float padding = gutter;
        const float avatar = c.dp(theme::kSmallAvatarSize);
        const float preview = std::max(avatar, c.lineHeight(theme::kCaptionSize) * 2);
        const ImVec2 cardMin{origin.x + gutter, y};
        const ImVec2 cardMax{origin.x + width - gutter,
                             y + padding * 2.5f + c.lineHeight(theme::kBodySize) + preview};
        c.rect(cardMin, cardMax, theme::kSurface, theme::kCardRadius);
        drawPressed(frame, cardMin, cardMax, theme::kCardRadius);
        float cy = y + padding;
        medium.text({cardMin.x + padding, cy}, theme::kBodySize, theme::kTextPrimary,
                    tr(Text::Comments));
        c.text({cardMin.x + padding + medium.measure(theme::kBodySize, tr(Text::Comments)).x +
                    gutter * 0.6f,
                cy},
               theme::kBodySize, theme::kTextSecondary, info->commentCount);
        cy += c.lineHeight(theme::kBodySize) + padding * 0.5f;
        const auto& comments = m_watch.comments;
        if (comments && !comments->comments.empty()) {
            const Comment& first = comments->comments.front();
            drawAvatar(frame, first.avatar, first.author,
                       {cardMin.x + padding + avatar * 0.5f, cy + avatar * 0.5f},
                       theme::kSmallAvatarSize);
            const float textX = cardMin.x + padding * 2 + avatar;
            c.paragraph({textX, cy}, cardMax.x - padding - textX, theme::kCaptionSize,
                        theme::kTextPrimary, first.text, 2);
        } else if (comments && comments->loading) {
            c.rect({cardMin.x + padding, cy}, {cardMax.x - padding * 4, cy + c.dp(10)},
                   theme::kPlaceholder, 4);
        }
        if (frame.touch.tapped(cardMin, cardMax)) {
            openSheet(Sheet::Comments);
        }
        y = cardMax.y + gutter;
    }
    return y - origin.y;
}

void App::drawChannelRow(Frame& frame, innertube::ChannelSummary& channel, ImVec2 origin,
                         float width) {
    const Canvas& c = frame.canvas;
    const Canvas medium = c.with(Weight::Medium);
    const float gutter = c.dp(theme::kGutter);
    const float height = c.dp(kChannelRowHeight);
    const float avatar = c.dp(theme::kAvatarSize);
    const float centerY = origin.y + height * 0.5f;

    const std::string_view label = channel.subscribed ? tr(Text::Subscribed) : tr(Text::Subscribe);
    const float buttonWidth = medium.measure(theme::kBodySize, label).x + gutter * 3;
    const ImVec2 buttonMax{origin.x + width - gutter, centerY + c.dp(theme::kButtonHeight) * 0.5f};
    const ImVec2 buttonMin{buttonMax.x - buttonWidth, buttonMax.y - c.dp(theme::kButtonHeight)};
    drawSubscribeButton(frame, channel, buttonMin, buttonMax);

    const ImVec2 rowMax{buttonMin.x - gutter, origin.y + height};
    drawPressed(frame, origin, rowMax);
    drawAvatar(frame, channel.avatar, channel.name, {origin.x + gutter + avatar * 0.5f, centerY},
               theme::kAvatarSize);
    const float textX = origin.x + gutter * 2 + avatar;
    const float textWidth = rowMax.x - textX;
    const float lines = c.lineHeight(theme::kBodySize) + c.lineHeight(theme::kCaptionSize);
    medium.line({textX, centerY - lines * 0.5f}, textWidth, theme::kBodySize, theme::kTextPrimary,
                channel.name);
    c.line({textX, centerY - lines * 0.5f + c.lineHeight(theme::kBodySize)}, textWidth,
           theme::kCaptionSize, theme::kTextSecondary, channel.subscribers);
    if (!channel.id.empty() && frame.touch.tapped(origin, rowMax)) {
        later([this, id = channel.id, name = channel.name] { openChannel(id, name); });
    }
}

void App::drawSubscribeButton(Frame& frame, innertube::ChannelSummary& channel, ImVec2 min,
                              ImVec2 max) {
    const std::string_view label = channel.subscribed ? tr(Text::Subscribed) : tr(Text::Subscribe);
    if (pillButton(frame, label, min, max, !channel.subscribed)) {
        toggleSubscription(channel);
    }
}

// --- Account actions
// ------------------------------------------------------------------------------

void App::rateVideo(Rating rating) {
    if (!m_session.signedIn()) {
        showToast(tr(Text::SignInToAct));
        return;
    }
    auto& info = *m_watch.info;
    const Rating previous = info.rating;
    info.rating = rating;
    if (rating != Rating::None) {
        m_watch.likePopAt = now();
    }
    m_tasks.run([this, videoId = info.videoId, rating, previous] {
        auto result = m_api.rate(videoId, rating);
        if (result) {
            return;
        }
        m_tasks.post([this, videoId, previous] {
            if (m_watch.info && m_watch.info->videoId == videoId) {
                m_watch.info->rating = previous;
            }
            showToast(tr(Text::ActionFailed));
        });
    });
}

void App::toggleSubscription(innertube::ChannelSummary& channel) {
    if (!m_session.signedIn()) {
        showToast(tr(Text::SignInToAct));
        return;
    }
    // The same channel may be shown on the watch page and on channel pages in the stack.
    const auto apply = [this](const std::string& id, bool subscribed) {
        if (m_watch.info && m_watch.info->channel.id == id) {
            m_watch.info->channel.subscribed = subscribed;
        }
        for (auto& screen : m_stack) {
            if (screen.channel && screen.channel->channel.summary.id == id) {
                screen.channel->channel.summary.subscribed = subscribed;
            }
        }
    };
    const bool subscribed = !channel.subscribed;
    apply(channel.id, subscribed);
    m_tasks.run([this, channel, subscribed, apply] {
        if (m_api.setSubscribed(channel, subscribed)) {
            return;
        }
        m_tasks.post([this, id = channel.id, subscribed, apply] {
            apply(id, !subscribed);
            showToast(tr(Text::ActionFailed));
        });
    });
}

// --- Comments
// --------------------------------------------------------------------------------------

void App::loadComments(const std::shared_ptr<CommentList>& list, std::string token) {
    if (token.empty() || list->loading) {
        return;
    }
    list->loading = true;
    list->error.clear();
    m_tasks.run([this, target = std::weak_ptr(list), token = std::move(token),
                 post = list->postComments] {
        auto page = post ? m_api.postComments(token) : m_api.comments(token);
        m_tasks.post([target, page = std::move(page)]() mutable {
            const auto list = target.lock();
            if (!list) {
                return;
            }
            list->loading = false;
            list->loaded = true;
            if (!page) {
                list->error = std::move(page.error().message);
                return;
            }
            // Replies pages restate the parent first; it is already shown.
            for (auto& comment : page->comments) {
                const bool duplicate = std::ranges::any_of(
                    list->comments, [&](const Comment& known) { return known.id == comment.id; });
                if (!duplicate) {
                    list->comments.push_back(std::move(comment));
                }
            }
            list->heights.resize(list->comments.size(), 0);
            list->expanded.resize(list->comments.size(), false);
            list->continuation = std::move(page->continuation);
        });
    });
}

void App::openReplies(const Comment& parent) {
    if (!m_stack.empty() && m_stack.back().post) {
        openPostReplies(parent);
        return;
    }
    // The parent leads the replies view, fully expanded.
    m_watch.replyParent = parent;
    m_watch.replies = std::make_shared<CommentList>();
    m_watch.replies->comments.push_back(parent);
    m_watch.replies->heights.push_back(0);
    m_watch.replies->expanded.push_back(true);
    loadComments(m_watch.replies, parent.repliesToken);
}

void App::rateComment(const std::shared_ptr<CommentList>& list, std::size_t index, Rating rating) {
    if (list->postComments) {
        showToast(tr(Text::PostActionUnavailable));
        return;
    }
    if (!m_session.signedIn()) {
        showToast(tr(Text::SignInToAct));
        return;
    }
    Comment& comment = list->comments[index];
    const Rating previous = comment.rating;
    comment.rating = rating;
    m_tasks.run([this, target = std::weak_ptr(list), videoId = m_watch.info->videoId,
                 commentId = comment.id, rating, previous] {
        if (m_api.rateComment(videoId, commentId, rating)) {
            return;
        }
        m_tasks.post([this, target, commentId, previous] {
            if (const auto list = target.lock()) {
                for (auto& known : list->comments) {
                    if (known.id == commentId) {
                        known.rating = previous;
                    }
                }
            }
            showToast(tr(Text::ActionFailed));
        });
    });
}

void App::submitComment() {
    const std::string text(m_draft.data());
    if (text.empty() || !m_watch.info) {
        return;
    }
    if (!m_session.signedIn()) {
        showToast(tr(Text::SignInToAct));
        return;
    }
    m_draft.fill(0);
    ImGui::ClearActiveID();

    // Shown immediately, as the official app does, and withdrawn if the post fails.
    const auto accounts = m_session.accounts();
    const auto& me = accounts.at(m_session.activeAccount());
    Comment local{
        .id = std::string(kLocalCommentPrefix) +
              std::to_string(static_cast<std::int64_t>(now() * 1000)),
        .author = me.handle.empty() ? me.name : me.handle,
        .avatar = me.photo,
        .text = text,
        .published = std::string(tr(Text::Posted)),
    };
    const bool reply = m_watch.replyParent.has_value();
    const auto list = reply ? m_watch.replies : m_watch.comments;
    if (!list) {
        return;
    }
    const auto at = list->comments.begin() + (reply ? 1 : 0); // Replies stay under the parent.
    list->comments.insert(at, local);
    list->heights.insert(list->heights.begin() + (reply ? 1 : 0), 0);
    list->expanded.insert(list->expanded.begin() + (reply ? 1 : 0), false);
    list->scroller.reset();

    m_tasks.run([this, target = std::weak_ptr(list), videoId = m_watch.info->videoId,
                 parentId = reply ? m_watch.replyParent->id : std::string(), text,
                 localId = local.id] {
        const auto result = parentId.empty() ? m_api.postComment(videoId, text)
                                             : m_api.reply(videoId, parentId, text);
        if (result) {
            return;
        }
        m_tasks.post([this, target, localId] {
            if (const auto list = target.lock()) {
                for (std::size_t i = 0; i < list->comments.size(); ++i) {
                    if (list->comments[i].id == localId) {
                        const auto offset = static_cast<std::ptrdiff_t>(i);
                        list->comments.erase(list->comments.begin() + offset);
                        list->heights.erase(list->heights.begin() + offset);
                        list->expanded.erase(list->expanded.begin() + offset);
                        break;
                    }
                }
            }
            showToast(tr(Text::ActionFailed));
        });
    });
}

bool App::drawCommentInput(Frame& frame, ImVec2 origin, float width, std::string_view hint) {
    const Canvas& c = frame.canvas;
    const float gutter = c.dp(theme::kGutter);
    const float height = c.dp(kRowHeight);
    const float avatar = c.dp(theme::kSmallAvatarSize);
    const float centerY = origin.y + height * 0.5f;

    const auto accounts = m_session.accounts();
    if (!accounts.empty()) {
        const auto& me = accounts[std::min(m_session.activeAccount(), accounts.size() - 1)];
        drawAvatar(frame, me.photo, me.name, {origin.x + gutter + avatar * 0.5f, centerY},
                   theme::kSmallAvatarSize);
    }
    const bool hasText = m_draft[0] != 0;
    const float send = hasText ? c.dp(48) : 0;
    const float fieldX = origin.x + gutter * 2 + avatar;
    const float fieldHeight = c.dp(theme::kSearchFieldHeight);

    if (!m_session.signedIn()) {
        const ImVec2 fieldMin{fieldX, centerY - fieldHeight * 0.5f};
        const ImVec2 fieldMax{origin.x + width - gutter, centerY + fieldHeight * 0.5f};
        c.rect(fieldMin, fieldMax, theme::kSurface, theme::kPillRadius);
        c.text({fieldMin.x + gutter, centerY - c.lineHeight(theme::kBodySize) * 0.5f},
               theme::kBodySize, theme::kTextSecondary, hint);
        if (frame.touch.tapped(fieldMin, fieldMax)) {
            showToast(tr(Text::SignInToAct));
        }
        return false;
    }

    ImGui::SetCursorScreenPos({fieldX, centerY - fieldHeight * 0.5f});
    ImGui::SetNextItemWidth(origin.x + width - gutter - send - fieldX);
    ImGui::PushFont(nullptr, c.dp(theme::kBodySize));
    const std::string hintText(hint);
    bool submitted = ImGui::InputTextWithHint("##comment", hintText.c_str(), m_draft.data(),
                                              m_draft.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopFont();
    if (hasText && iconButton(frame, Icon::Send, {origin.x + width - gutter - send * 0.5f, centerY},
                              theme::kLink)) {
        submitted = true;
    }
    return submitted;
}

float App::drawComment(Frame& frame, const std::shared_ptr<CommentList>& list, std::size_t index,
                       ImVec2 origin, float width, bool allowReplies) {
    const Canvas& c = frame.canvas;
    const Canvas medium = c.with(Weight::Medium);
    const float gutter = c.dp(theme::kGutter);
    const float avatar = c.dp(theme::kSmallAvatarSize);
    const ImVec2 clipMin = c.list()->GetClipRectMin();
    const ImVec2 clipMax = c.list()->GetClipRectMax();
    Comment& comment = list->comments[index];

    float y = origin.y + gutter * 0.75f;
    drawAvatar(frame, comment.avatar, comment.author,
               {origin.x + gutter + avatar * 0.5f, y + avatar * 0.5f}, theme::kSmallAvatarSize);
    const float textX = origin.x + gutter * 2 + avatar;
    const float textWidth = origin.x + width - gutter - textX;
    c.line({textX, y}, textWidth, theme::kCaptionSize, theme::kTextSecondary,
           comment.author + std::string(kBullet) + comment.published);
    y += c.lineHeight(theme::kCaptionSize) + c.dp(2);

    const bool expanded = list->expanded[index];
    const int maxLines = expanded ? kExpandedCommentLines : kCollapsedCommentLines;
    const float bodyTop = y;
    const float body = c.paragraph({textX, y}, textWidth, theme::kBodySize, theme::kTextPrimary,
                                   comment.text, maxLines);
    y += body;
    const bool truncated =
        !expanded && body >= c.lineHeight(theme::kBodySize) * kCollapsedCommentLines - 1;
    if (truncated) {
        c.text({textX, y}, theme::kBodySize, theme::kTextSecondary, tr(Text::ReadMore));
        y += c.lineHeight(theme::kBodySize);
    }
    if ((truncated || expanded) &&
        tappedWithin(frame, {textX, bodyTop}, {origin.x + width, y}, clipMin, clipMax)) {
        list->expanded[index] = !expanded;
    }

    // Actions: like (with count), dislike, reply.
    const float actions = c.dp(kActionHeight);
    const float slot = c.dp(40);
    const float centerY = y + actions * 0.5f;
    const bool liked = comment.rating == Rating::Like;
    const bool disliked = comment.rating == Rating::Dislike;
    float x = textX - c.dp(8);
    const ImVec2 likeMin{x, y};
    c.icon(liked ? Icon::ThumbUpFilled : Icon::ThumbUp,
           {x + c.dp(kActionIcon) * 0.5f + c.dp(8), centerY}, kActionIcon, theme::kTextPrimary);
    x += c.dp(kActionIcon) + c.dp(14);
    c.text({x, centerY - c.lineHeight(theme::kCaptionSize) * 0.5f}, theme::kCaptionSize,
           theme::kTextSecondary, comment.likes);
    x += c.measure(theme::kCaptionSize, comment.likes).x + c.dp(8);
    if (tappedWithin(frame, likeMin, {x, y + actions}, clipMin, clipMax)) {
        later([this, list, index, rating = toggled(comment.rating, Rating::Like)] {
            rateComment(list, index, rating);
        });
    }
    const ImVec2 dislikeMin{x, y};
    c.icon(disliked ? Icon::ThumbDownFilled : Icon::ThumbDown, {x + slot * 0.5f, centerY},
           kActionIcon, theme::kTextPrimary);
    x += slot;
    if (tappedWithin(frame, dislikeMin, {x, y + actions}, clipMin, clipMax)) {
        later([this, list, index, rating = toggled(comment.rating, Rating::Dislike)] {
            rateComment(list, index, rating);
        });
    }
    const ImVec2 replyMin{x, y};
    c.icon(Icon::Comment, {x + slot * 0.5f, centerY}, kActionIcon, theme::kTextPrimary);
    x += slot;
    const bool canReply = !comment.id.starts_with(kLocalCommentPrefix);
    if (canReply && tappedWithin(frame, replyMin, {x, y + actions}, clipMin, clipMax)) {
        later([this, comment, allowReplies, post = list->postComments] {
            if (post) {
                showToast(tr(Text::PostActionUnavailable));
                return;
            }
            if (allowReplies) {
                openReplies(comment);
            }
            ImGui::SetKeyboardFocusHere(); // Applies to the reply field drawn next frame.
        });
    }
    y += actions;

    if (allowReplies && comment.replyCount > 0) {
        const std::string label = replyLabel(comment.replyCount);
        const float labelWidth = medium.measure(theme::kBodySize, label).x;
        medium.text({textX, y + c.dp(6)}, theme::kBodySize, theme::kTextPrimary, label);
        c.icon(Icon::ChevronRight,
               {textX + labelWidth + c.dp(10), y + c.dp(6) + c.lineHeight(theme::kBodySize) * 0.5f},
               14, theme::kLink);
        const float rowBottom = y + c.dp(kActionHeight);
        if (tappedWithin(frame, {textX - c.dp(8), y}, {textX + labelWidth + c.dp(24), rowBottom},
                         clipMin, clipMax)) {
            later([this, comment] { openReplies(comment); });
        }
        y = rowBottom;
    }
    return y + gutter * 0.5f - origin.y;
}

void App::drawCommentList(Frame& frame, const std::shared_ptr<CommentList>& list, ImVec2 min,
                          ImVec2 max, bool allowReplies, const ListHeader& header) {
    const Canvas& c = frame.canvas;
    const float width = max.x - min.x;
    const float estimate = c.dp(kEstimatedComment);
    const bool more = list->loading || !list->continuation.empty();
    const float footer = more ? c.dp(56) : 0;

    float content = header.height + footer;
    for (const float height : list->heights) {
        content += height > 0 ? height : estimate;
    }
    list->scroller.update(frame.touch, min, max, content, frame.seconds, c.dp(1));
    frame.animating |= list->scroller.moving();
    const float offset = list->scroller.offset();

    c.list()->PushClipRect(min, max, true);
    float y = min.y - offset;
    if (header.draw && y + header.height > min.y) {
        header.draw(frame, {min.x, y}, width);
    }
    y += header.height;
    for (std::size_t i = 0; i < list->comments.size(); ++i) {
        float height = list->heights[i] > 0 ? list->heights[i] : estimate;
        if (y + height >= min.y && y < max.y) {
            height = drawComment(frame, list, i, {min.x, y}, width, allowReplies);
            list->heights[i] = height;
        }
        y += height;
    }
    if (list->comments.empty() && !list->loading) {
        const std::string_view message =
            !list->error.empty() ? std::string_view(list->error) : tr(Text::Nothing);
        c.paragraph({min.x + c.dp(theme::kGutter), y + c.dp(24)}, width - c.dp(theme::kGutter) * 2,
                    theme::kBodySize, theme::kTextSecondary, message, 3, TextAlign::Center);
    }
    if (more && y < max.y) {
        c.spinner({min.x + width * 0.5f, y + footer * 0.5f}, theme::kSpinnerSize,
                  theme::kTextSecondary, frame.time);
        frame.animating = true;
    }
    c.list()->PopClipRect();

    if (!list->loading && !list->continuation.empty() && offset + (max.y - min.y) * 2 >= content) {
        loadComments(list, list->continuation);
    }
}

void App::drawCommentsSheet(Frame& frame, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const Canvas medium = c.with(Weight::Medium);
    const float gutter = c.dp(theme::kGutter);
    const float width = max.x - min.x;
    const float row = c.dp(kRowHeight);
    const bool replies = m_watch.replies != nullptr;
    auto& info = *m_watch.info;

    // Header (drawn after the list, below, so it stays on top; laid out here).
    const float headerTop = min.y + c.dp(12);
    const float chipsTop = headerTop + row;
    const float inputTop = chipsTop + (replies ? 0 : c.dp(theme::kChipHeight) + gutter);
    const float listTop = inputTop + row + c.dp(4);

    if (!m_watch.comments) {
        m_watch.comments = std::make_shared<CommentList>();
        loadComments(m_watch.comments,
                     m_watch.newestFirst ? info.newestCommentsToken : info.commentsToken);
    }
    const auto& list = replies ? m_watch.replies : m_watch.comments;

    // Input first, so the list underneath never steals its taps.
    if (drawCommentInput(frame, {min.x, inputTop}, width,
                         tr(replies ? Text::AddReply : Text::AddComment))) {
        later([this] { submitComment(); });
    }
    c.list()->AddLine({min.x, listTop - c.dp(1)}, {max.x, listTop - c.dp(1)}, theme::kDivider);
    // Rows clamp their hit boxes to the list's clip rectangle, so the header keeps its taps.
    drawCommentList(frame, list, {min.x, listTop}, max, !replies);

    // Header row.
    const float centerY = headerTop + row * 0.5f;
    float titleX = min.x + gutter * 1.5f;
    if (replies) {
        if (iconButton(frame, Icon::Back, {min.x + row * 0.5f, centerY}, theme::kTextPrimary)) {
            later([this] {
                m_watch.replies.reset();
                m_watch.replyParent.reset();
            });
        }
        titleX = min.x + row + gutter;
    }
    const std::string_view title = tr(replies ? Text::Replies : Text::Comments);
    const Canvas heading = c.with(theme::kHeadingWeight);
    heading.text({titleX, centerY - c.lineHeight(theme::kSheetTitleSize) * 0.5f},
                 theme::kSheetTitleSize, theme::kTextPrimary, title);
    if (!replies) {
        c.text({titleX + heading.measure(theme::kSheetTitleSize, title).x + gutter,
                centerY - c.lineHeight(theme::kBodySize) * 0.5f},
               theme::kBodySize, theme::kTextSecondary, info.commentCount);
    }
    if (iconButton(frame, Icon::Close, {max.x - row * 0.5f, centerY}, theme::kTextPrimary)) {
        later([this] { closeSheet(); });
    }

    // Sort chips.
    if (!replies) {
        float x = min.x + gutter;
        const float chip = c.dp(theme::kChipHeight);
        for (const bool newest : {false, true}) {
            const std::string_view label = tr(newest ? Text::NewestComments : Text::TopComments);
            const float chipWidth = medium.measure(theme::kBodySize, label).x + gutter * 2;
            const ImVec2 chipMin{x, chipsTop};
            const ImVec2 chipMax{x + chipWidth, chipsTop + chip};
            const bool selected = m_watch.newestFirst == newest;
            c.rect(chipMin, chipMax, selected ? theme::kTextPrimary : theme::kSurface,
                   theme::kCornerRadius);
            drawPressed(frame, chipMin, chipMax, theme::kCornerRadius);
            medium.text({x + gutter, chipsTop + (chip - c.lineHeight(theme::kBodySize)) * 0.5f},
                        theme::kBodySize, selected ? theme::kTextOnLight : theme::kTextPrimary,
                        label);
            if (!selected && frame.touch.tapped(chipMin, chipMax)) {
                later([this, newest] {
                    m_watch.newestFirst = newest;
                    m_watch.comments = std::make_shared<CommentList>();
                    loadComments(m_watch.comments, newest ? m_watch.info->newestCommentsToken
                                                          : m_watch.info->commentsToken);
                });
            }
            x = chipMax.x + gutter * 0.75f;
        }
    }
}

// --- Description
// -----------------------------------------------------------------------------------

void App::drawDescriptionSheet(Frame& frame, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const Canvas medium = c.with(Weight::Medium);
    const float gutter = c.dp(theme::kGutter);
    const float width = max.x - min.x;
    const float row = c.dp(kRowHeight);
    auto& info = *m_watch.info;

    const float headerTop = min.y + c.dp(12);
    const ImVec2 bodyMin{min.x, headerTop + row};
    auto& scroller = m_watch.descriptionScroller;
    scroller.update(frame.touch, bodyMin, max, m_watch.descriptionHeight, frame.seconds, c.dp(1));
    frame.animating |= scroller.moving();

    c.list()->PushClipRect(bodyMin, max, true);
    const float top = bodyMin.y + gutter - scroller.offset();
    float y = top;
    const Canvas heading = c.with(theme::kHeadingWeight);
    y += heading.paragraph({min.x + gutter, y}, width - gutter * 2, theme::kWatchTitleSize,
                           theme::kTextPrimary, info.title, 4);
    y += gutter * 1.5f;

    // Factoids: likes, views and date side by side, as large figures with small labels.
    const std::array<std::pair<std::string_view, std::string_view>, 3> facts{{
        {info.likes, tr(Text::Likes)},
        {info.views, tr(Text::Views)},
        {info.date, ""},
    }};
    const float column = (width - gutter * 2) / static_cast<float>(facts.size());
    for (std::size_t i = 0; i < facts.size(); ++i) {
        const float x = min.x + gutter + column * static_cast<float>(i);
        medium.paragraph({x, y}, column - gutter, theme::kTitleSize, theme::kTextPrimary,
                         facts[i].first, 2, TextAlign::Center);
        c.line({x, y + c.lineHeight(theme::kTitleSize) * 2}, column - gutter, theme::kCaptionSize,
               theme::kTextSecondary, facts[i].second, TextAlign::Center);
    }
    y += c.lineHeight(theme::kTitleSize) * 2 + c.lineHeight(theme::kCaptionSize) + gutter;
    c.rect({min.x + gutter, y}, {max.x - gutter, y + c.dp(1)}, theme::kDivider);
    y += gutter;

    const std::string& description =
        !info.description.empty() ? info.description
                                  : (m_watch.details ? m_watch.details->description : info.title);
    y += c.paragraph({min.x + gutter, y}, width - gutter * 2, theme::kBodySize, theme::kTextPrimary,
                     description, kDescriptionLines);
    y += gutter;
    drawChannelRow(frame, info.channel, {min.x, y}, width);
    y += c.dp(kChannelRowHeight) + gutter * 2;
    c.list()->PopClipRect();
    m_watch.descriptionHeight = y - top + gutter;

    const float centerY = headerTop + row * 0.5f;
    heading.text({min.x + gutter * 1.5f, centerY - c.lineHeight(theme::kSheetTitleSize) * 0.5f},
                 theme::kSheetTitleSize, theme::kTextPrimary, tr(Text::Description));
    if (iconButton(frame, Icon::Close, {max.x - row * 0.5f, centerY}, theme::kTextPrimary)) {
        later([this] { closeSheet(); });
    }
}

} // namespace sloptube::app
