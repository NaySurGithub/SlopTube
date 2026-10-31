#pragma once

#include "core/innertube/ClientProfile.hpp"
#include "core/innertube/Types.hpp"
#include "core/json/Json.hpp"
#include "core/util/Result.hpp"

#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <string_view>

namespace sloptube::http {
class Client;
}

namespace sloptube::auth {
class Session;
}

namespace sloptube::innertube {

enum class Feed : std::uint8_t {
    Home,
    Subscriptions,
    History,
    Playlists,
    Channels, // Subscribed channels, alphabetically.
};

struct Locale {
    std::string language = "en"; // ISO 639-1, e.g. "fr".
    std::string region = "US";   // ISO 3166-1 alpha-2, e.g. "FR".
};

class Api {
public:
    Api(const http::Client& http, auth::Session& session, Locale locale);

    [[nodiscard]] Result<Page> feed(Feed feed);
    [[nodiscard]] Result<Page> search(std::string_view query, const SearchFilters& filters = {});
    // Completions of a partly typed query, best first (the suggestion service, not InnerTube).
    [[nodiscard]] Result<std::vector<std::string>> searchSuggestions(std::string_view query);
    // Shorts picked for the signed-in account (or popular ones when signed out).
    [[nodiscard]] Result<Page> shorts();
    [[nodiscard]] Result<Page> playlist(std::string_view playlistId);
    // The videos a mix plays, in order, from the first (see isMix).
    [[nodiscard]] Result<Page> mix(std::string_view playlistId);
    [[nodiscard]] Result<Watch> watch(std::string_view videoId);
    // First page from a Watch comments token, or the next page / replies from a Comment token.
    [[nodiscard]] Result<CommentPage> comments(std::string_view token);
    [[nodiscard]] Result<Channel> channel(std::string_view channelId);
    // One tab of a channel, read anonymously through MWEB. Empty when the channel has no such
    // tab (the server answers with the home tab instead).
    [[nodiscard]] Result<Page> channelTab(std::string_view channelId, ChannelTab tab);
    // Channel id of a handle ("@name").
    [[nodiscard]] Result<std::string> channelIdOf(std::string_view handle);
    // Avatar of any channel without signing in; cheap enough to resolve feed avatars lazily.
    [[nodiscard]] Result<std::string> channelAvatar(std::string_view channelId);
    // Uploader of a video, for feeds whose items do not link to it (one small anonymous request).
    [[nodiscard]] Result<std::string> videoChannel(std::string_view videoId);
    // Seek-preview sheets of a video.
    [[nodiscard]] Result<Storyboard> storyboard(std::string_view videoId);
    [[nodiscard]] Result<std::vector<Account>> accounts();
    // Playlists of the signed-in account that "Save" offers for `videoId`.
    [[nodiscard]] Result<std::vector<PlaylistOption>> saveOptions(std::string_view videoId);
    // Next page of `page`; requires a non-empty `page.continuation`.
    [[nodiscard]] Result<Page> continuation(const Page& page);
    [[nodiscard]] Result<Player> player(std::string_view videoId);
    // Subtitle tracks of a video, and the languages they can be translated into.
    [[nodiscard]] Result<Captions> captions(std::string_view videoId);

    // Community posts are read anonymously through MWEB: the TV client has none, and posts need
    // no account.
    [[nodiscard]] Result<PostPage> channelPosts(std::string_view channelId);
    // A post's own page: the post and its comments token (`Post::detailParams`).
    [[nodiscard]] Result<Post> postDetail(std::string_view detailParams);
    // A page of a post's comments: the first from `Post::commentsToken`, then continuations and
    // replies.
    [[nodiscard]] Result<CommentPage> postComments(std::string_view token);
    // Recent posts of subscribed channels, interleaved channel by channel: the `preferred`
    // channels first (those of the subscriptions feed, by latest activity), then the others.
    [[nodiscard]] Result<std::vector<Post>>
    subscriptionPosts(std::span<const std::string> preferred);

    // Chat of a live stream: the first page's token is built from the ids, without the `next`
    // response (only WEB's carries it, at about 760 KB). Read anonymously through TV.
    [[nodiscard]] static std::string liveChatToken(std::string_view channelId,
                                                   std::string_view videoId);
    [[nodiscard]] Result<ChatPage> liveChat(std::string_view token);

    // Actions on the signed-in account.
    [[nodiscard]] Result<void> setSaved(std::string_view playlistId, std::string_view videoId,
                                        bool saved);
    // Creates a playlist holding `videoId`; returns its id.
    [[nodiscard]] Result<std::string> createPlaylist(std::string_view title, Privacy privacy,
                                                     std::string_view videoId);
    [[nodiscard]] Result<void> rate(std::string_view videoId, Rating rating);
    [[nodiscard]] Result<void> setSubscribed(const ChannelSummary& channel, bool subscribed);
    [[nodiscard]] Result<void> rateComment(std::string_view videoId, std::string_view commentId,
                                           Rating rating);
    [[nodiscard]] Result<void> postComment(std::string_view videoId, std::string_view text);
    [[nodiscard]] Result<void> reply(std::string_view videoId, std::string_view commentId,
                                     std::string_view text);

private:
    // Sends `body` (an open object holding the endpoint-specific fields) with the client context
    // appended. The returned element is valid until the next JSON parse on the calling thread.
    [[nodiscard]] Result<json::Element> post(Endpoint endpoint, ClientKind client,
                                             json::Writer body);
    [[nodiscard]] Result<Page> browse(std::string_view browseId);
    [[nodiscard]] Result<Page> pageRequest(Endpoint endpoint, json::Writer body);
    // A page of the Shorts sequence, each short completed with its title and uploader.
    [[nodiscard]] Result<Page> reelSequence(std::string_view token);
    // Metadata request whose response must not carry an error object.
    [[nodiscard]] Result<json::Element> checked(Endpoint endpoint, ClientKind client,
                                                json::Writer body);

    const http::Client& m_http;
    auth::Session& m_session;
    Locale m_locale;
    std::mutex m_visitorMutex;
    std::string m_visitorData;
};

// Appends the continuation page `next` to `page`. Some feeds (the account's playlists) answer
// every continuation with their first page again, so items already present are dropped and a
// page bringing nothing new ends the feed.
void appendPage(Page& page, Page next);

} // namespace sloptube::innertube
