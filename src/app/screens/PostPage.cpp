// Community posts: cards interleaved with the home feed, and a post's own page with its comments.

#include "app/App.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"

#include <algorithm>

namespace sloptube::app {
namespace {

using innertube::Comment;
using innertube::Post;

// Home feed placement: a post after the 4th video, then one every 6 videos, which falls between
// the Shorts shelves (after videos 1, 13, 25, ...).
constexpr std::size_t kFirstPostAfter = 4;
constexpr std::size_t kPostEvery = 6;
constexpr float kCommentsTitleHeight = 48; // dp, "Comments" row between the post and the list.

} // namespace

void App::loadPosts() {
    const Feed& home = *m_tabFeeds[std::to_underlying(Tab::Home)];
    const Feed& subscriptions = *m_tabFeeds[std::to_underlying(Tab::Subscriptions)];
    // Waits for the home feed (posts come second) and the subscriptions feed, whose uploaders,
    // newest activity first, are the channels whose posts are shown.
    if (m_posts->requested || !m_session.signedIn() || home.page.items.empty() ||
        !subscriptions.loaded) {
        return;
    }
    m_posts->requested = true;
    std::vector<std::string> channels;
    for (const auto& item : subscriptions.page.items) {
        if (!item.channelId.empty() &&
            std::ranges::find(channels, item.channelId) == channels.end()) {
            channels.push_back(item.channelId);
        }
    }
    m_tasks.run([this, target = std::weak_ptr(m_posts), channels = std::move(channels)] {
        auto posts = m_api.subscriptionPosts(channels);
        m_tasks.post([target, posts = std::move(posts)]() mutable {
            const auto feed = target.lock();
            if (!feed || !posts) {
                return; // Posts are extras: the feed is complete without them.
            }
            for (auto& post : *posts) {
                feed->cards.push_back(PostCard{.post = std::move(post)});
            }
        });
    });
}

void App::addPostInserts(const Canvas& c, float width, std::vector<ListInsert>& inserts) {
    for (std::size_t index = 0; index < m_posts->cards.size(); ++index) {
        inserts.push_back({
            .after = kFirstPostAfter + index * kPostEvery,
            .height = postCardHeight(c, m_posts->cards[index], width, PostLayout::Feed),
            .draw =
                [this, posts = m_posts, index](Frame& frame, ImVec2 origin, float cardWidth) {
                    PostCard& card = posts->cards[index];
                    onPostAction(drawPostCard(frame, card, origin, cardWidth, PostLayout::Feed),
                                 card.post);
                },
        });
    }
    std::ranges::stable_sort(inserts, {}, &ListInsert::after);
}

void App::onPostAction(PostAction action, const Post& post) {
    switch (action) {
    case PostAction::None:
        break;
    case PostAction::Open:
        later([this, post] { openPost(post); });
        break;
    case PostAction::OpenChannel:
        later([this, id = post.channelId, name = post.author] { openChannel(id, name); });
        break;
    case PostAction::OpenVideo:
        if (post.video) {
            later([this, video = *post.video] { openVideo(video); });
        }
        break;
    case PostAction::Rate:
        // Rating a post needs parameters only signed-in web clients receive.
        showToast(tr(Text::PostActionUnavailable));
        break;
    }
}

void App::openPost(const Post& post) {
    auto page = std::make_shared<PostScreen>();
    page->card.post = post;
    page->card.expanded = true;
    page->comments->postComments = true;
    page->comments->loading = true;
    push(Screen{.route = Route::Post,
                .argument = post.id,
                .title = std::string(tr(Text::Post)),
                .post = page});

    // Only the post's own page carries the token of its comments.
    m_tasks.run([this, target = std::weak_ptr(page), params = post.detailParams] {
        Result<Post> detail = fail("post: no page");
        if (!params.empty()) {
            detail = m_api.postDetail(params);
        }
        m_tasks.post([this, target, detail = std::move(detail)]() mutable {
            const auto page = target.lock();
            if (!page) {
                return;
            }
            auto& comments = page->comments;
            comments->loading = false;
            if (!detail || detail->commentsToken.empty()) {
                comments->loaded = true;
                comments->error =
                    detail ? std::string(tr(Text::CommentsOff)) : std::move(detail.error().message);
                return;
            }
            page->card.post.commentsToken = detail->commentsToken;
            loadComments(comments, std::move(detail->commentsToken));
        });
    });
}

void App::openPostReplies(const Comment& parent) {
    auto& page = m_stack.back().post;
    // The parent leads the replies view, fully expanded, as on the watch page.
    page->replies = std::make_shared<CommentList>();
    page->replies->postComments = true;
    page->replies->comments.push_back(parent);
    page->replies->heights.push_back(0);
    page->replies->expanded.push_back(true);
    loadComments(page->replies, parent.repliesToken);
}

void App::drawPost(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const float width = max.x - min.x;
    const ImVec2 contentMin{min.x, min.y + c.dp(theme::kTopBarHeight)};
    PostScreen& page = *screen.post;
    if (page.replies) {
        drawCommentList(frame, page.replies, contentMin, max, false);
        drawTitleBar(frame, tr(Text::Replies), {min.x, 0}, width);
        return;
    }

    const float cardHeight = postCardHeight(c, page.card, width, PostLayout::Page);
    const ListHeader header{
        .height = cardHeight + c.dp(kCommentsTitleHeight),
        .draw =
            [this, &page, cardHeight](Frame& f, ImVec2 origin, float w) {
                const Canvas& canvas = f.canvas;
                const auto action = drawPostCard(f, page.card, origin, w, PostLayout::Page);
                if (action != PostAction::Open) { // Already open.
                    onPostAction(action, page.card.post);
                }
                const float top = origin.y + cardHeight;
                canvas.list()->AddLine({origin.x, top}, {origin.x + w, top}, theme::kDivider,
                                       canvas.dp(1));
                const float gutter = canvas.dp(theme::kGutter);
                const float textY =
                    top +
                    (canvas.dp(kCommentsTitleHeight) - canvas.lineHeight(theme::kTitleSize)) * 0.5f;
                const Canvas medium = canvas.with(Weight::Medium);
                medium.text({origin.x + gutter, textY}, theme::kTitleSize, theme::kTextPrimary,
                            tr(Text::Comments));
                canvas.text({origin.x + gutter * 1.5f +
                                 medium.measure(theme::kTitleSize, tr(Text::Comments)).x,
                             textY},
                            theme::kTitleSize, theme::kTextSecondary, page.card.post.commentCount);
            },
    };
    drawCommentList(frame, page.comments, contentMin, max, true, header);
    drawTitleBar(frame, screen.title, {min.x, 0}, width);
}

} // namespace sloptube::app
