#include "core/innertube/Api.hpp"

#include "core/auth/Session.hpp"
#include "core/http/Client.hpp"
#include "core/innertube/Parser.hpp"
#include "core/util/Protobuf.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace sloptube::innertube {
namespace {

constexpr std::string_view kBaseUrl = "https://youtubei.googleapis.com/youtubei/v1/";
constexpr std::string_view kQuery = "?prettyPrint=false";

constexpr std::array<std::string_view, 17> kEndpointPaths{
    "browse",
    "next",
    "player",
    "search",
    "like/like",
    "like/dislike",
    "like/removelike",
    "subscription/subscribe",
    "subscription/unsubscribe",
    "comment/create_comment",
    "comment/create_comment_reply",
    "comment/perform_comment_action",
    "account/accounts_list",
    "playlist/get_add_to_playlist",
    "browse/edit_playlist",
    "playlist/create",
    "reel/reel_watch_sequence",
};
static_assert(kEndpointPaths.size() == std::to_underlying(Endpoint::ReelSequence) + 1);

constexpr std::string_view kAddVideoAction = "ACTION_ADD_VIDEO";
constexpr std::string_view kRemoveVideoAction = "ACTION_REMOVE_VIDEO_BY_VIDEO_ID";

constexpr std::array<Endpoint, 3> kRatingEndpoints{
    Endpoint::RemoveLike,
    Endpoint::Like,
    Endpoint::Dislike,
};

constexpr std::string_view kAccountsRequestType = "ACCOUNTS_LIST_REQUEST_TYPE_CHANNEL_SWITCHER";

// Comment params are protobuf messages (field numbers as the web client encodes them).
namespace comment_params {
constexpr std::uint32_t kVideoId = 2;
constexpr std::uint32_t kCommentId = 4;
constexpr std::uint32_t kOptions = 5;
constexpr std::uint32_t kOptionsIndex = 1;
constexpr std::uint32_t kVariant = 10;
constexpr std::uint64_t kVariantValue = 7;

constexpr std::uint32_t kActionType = 1;
constexpr std::uint32_t kActionSurface = 2;
constexpr std::uint32_t kActionCommentId = 3;
constexpr std::uint32_t kActionVideoId = 5;
constexpr std::uint64_t kActionSurfaceValue = 2;
// Indexed by Rating: clearing a vote, like, dislike.
constexpr std::array<std::uint64_t, 3> kActionTypes{6, 5, 4};
} // namespace comment_params

constexpr std::string_view kFailedAction = "STATUS_FAILED";
constexpr std::string_view kSucceeded = "STATUS_SUCCEEDED";

constexpr std::array<std::string_view, 5> kFeedBrowseIds{
    "FEwhat_to_watch", "FEsubscriptions", "FEhistory", "FEplaylist_aggregation", "FEchannels",
};
static_assert(kFeedBrowseIds.size() == std::to_underlying(Feed::Channels) + 1);

// Search filter selecting Shorts; with an empty query the server picks them for the user. The TV
// client now mixes long videos in and gives a single page, so only its shorts are kept, and the
// first one seeds the Shorts sequence (MWEB), which continues without end like the official
// Shorts tab.
constexpr std::string_view kShortsSearchParams = "EgIQCQ==";

// Sequence parameters, as the official web client sends them: the seed short, then constant
// options.
namespace reel_params {
constexpr std::uint32_t kVideoId = 1;
constexpr std::uint32_t kOptions = 5;
constexpr std::uint32_t kOptionsSurface = 3;
constexpr std::uint64_t kOptionsSurfaceValue = 5;
constexpr std::uint32_t kPageSize = 10;
constexpr std::uint64_t kPageSizeValue = 25;
constexpr std::uint32_t kFlags = 13;
constexpr std::uint64_t kFlagsValue = 0;
} // namespace reel_params

std::string reelSequenceParams(std::string_view seedVideoId) {
    using namespace reel_params;
    protobuf::Writer options;
    options.varint(kOptionsSurface, kOptionsSurfaceValue);
    protobuf::Writer params;
    params.bytes(kVideoId, seedVideoId)
        .message(kOptions, options)
        .varint(kPageSize, kPageSizeValue)
        .varint(kFlags, kFlagsValue);
    return protobuf::base64Url(params.data());
}

// The channel's search tab, queried for nothing: the smallest page (about 30 KB, against
// several hundred for the home tab) that still carries the header with the avatar.
constexpr std::string_view kChannelSearchTabParams = "EgZzZWFyY2jyBgQKAloA";

// A channel's posts tab (MWEB: about 20 KB without posts, 70-150 KB for its latest 10).
constexpr std::string_view kPostsTabParams = "EgVwb3N0c_IGBAoCSgA%3D";
// Home feed posts: a handful of channels, the newest few posts of each, so the requests stay
// few and run once in the background.
constexpr std::size_t kPostChannels = 6;
constexpr std::size_t kPostsPerChannel = 3;

// Browse ids of a playlist page are the playlist id with this prefix.
constexpr std::string_view kPlaylistBrowsePrefix = "VL";

// Metadata uses the TV client: its responses are compact JSON and it accepts the OAuth token.
constexpr ClientKind kMetadataClient = ClientKind::Tv;

// Of the clients with fully downloadable streams, only Android lists subtitle tracks, and their
// timedtext addresses work without a proof-of-origin token.
constexpr ClientKind kCaptionsClient = ClientKind::Android;
// VISIONOS, MWEB and TV send no storyboards; Android and iOS do.
constexpr ClientKind kStoryboardClient = ClientKind::Android;

// Endpoints that only read, so a stalled request can be sent again; actions never are.
constexpr bool isRead(Endpoint endpoint) {
    switch (endpoint) {
    case Endpoint::Browse:
    case Endpoint::Next:
    case Endpoint::Player:
    case Endpoint::Search:
    case Endpoint::Accounts:
    case Endpoint::AddToPlaylistOptions:
    case Endpoint::ReelSequence:
        return true;
    default:
        return false;
    }
}

json::Writer openBody() {
    json::Writer body;
    body.beginObject();
    return body;
}

} // namespace

Api::Api(const http::Client& http, auth::Session& session, Locale locale)
    : m_http(http)
    , m_session(session)
    , m_locale(std::move(locale)) {}

Result<Page> Api::feed(Feed feed) {
    return browse(kFeedBrowseIds[std::to_underlying(feed)]);
}

Result<Page> Api::search(std::string_view query) {
    auto body = openBody();
    body.field("query", query);
    return pageRequest(Endpoint::Search, std::move(body));
}

Result<Page> Api::shorts() {
    auto body = openBody();
    body.field("query", "").field("params", kShortsSearchParams);
    auto page = pageRequest(Endpoint::Search, std::move(body));
    if (!page) {
        return page;
    }
    std::erase_if(page->items, [](const Item& item) { return item.kind != ItemKind::Short; });
    if (page->items.empty()) {
        return fail("search: no shorts");
    }
    page->continuation = reelSequenceParams(page->items.front().id);
    page->source = Endpoint::ReelSequence;
    return page;
}

Result<Page> Api::reelSequence(std::string_view token) {
    auto body = openBody();
    body.field("sequenceParams", token);
    auto root = checked(Endpoint::ReelSequence, ClientKind::Mweb, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    Page page = parser::reelSequence(*root);
    page.source = Endpoint::ReelSequence;
    // Sequence entries carry no title; the uploader's MWEB `player` (~13 KB) does. Shorts it
    // cannot describe are dropped.
    std::erase_if(page.items, [&](Item& item) {
        auto details = openBody();
        details.field("videoId", item.id);
        auto player = checked(Endpoint::Player, ClientKind::Mweb, std::move(details));
        if (player) {
            parser::videoDetails(*player, item);
        }
        return item.title.empty();
    });
    return page;
}

Result<Page> Api::playlist(std::string_view playlistId) {
    std::string browseId(kPlaylistBrowsePrefix);
    browseId += playlistId;
    return browse(browseId);
}

Result<Watch> Api::watch(std::string_view videoId) {
    auto body = openBody();
    body.field("videoId", videoId);
    auto root = checked(Endpoint::Next, kMetadataClient, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    return parser::watch(*root);
}

Result<CommentPage> Api::comments(std::string_view token) {
    auto body = openBody();
    body.field("continuation", token);
    auto root = checked(Endpoint::Next, kMetadataClient, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    return parser::comments(*root);
}

Result<Channel> Api::channel(std::string_view channelId) {
    auto body = openBody();
    body.field("browseId", channelId);
    auto root = checked(Endpoint::Browse, kMetadataClient, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    auto result = parser::channel(*root);
    result.summary.id = channelId;
    return result;
}

Result<std::string> Api::channelAvatar(std::string_view channelId) {
    auto body = openBody();
    body.field("browseId", channelId).field("params", kChannelSearchTabParams).field("query", "");
    auto root = checked(Endpoint::Browse, ClientKind::Mweb, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    return parser::channelAvatar(*root);
}

Result<Storyboard> Api::storyboard(std::string_view videoId) {
    auto body = openBody();
    body.field("videoId", videoId);
    auto root = checked(Endpoint::Player, kStoryboardClient, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    return parser::storyboard(
        json::string(*root, "/storyboards/playerStoryboardSpecRenderer/spec"));
}

Result<std::string> Api::videoChannel(std::string_view videoId) {
    auto body = openBody();
    body.field("videoId", videoId);
    auto root = checked(Endpoint::Player, ClientKind::Mweb, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    const auto channelId = parser::videoChannel(*root);
    if (channelId.empty()) {
        return fail("player: no channel");
    }
    return std::string(channelId);
}

Result<PostPage> Api::channelPosts(std::string_view channelId) {
    auto body = openBody();
    body.field("browseId", channelId).field("params", kPostsTabParams);
    auto root = checked(Endpoint::Browse, ClientKind::Mweb, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    return parser::posts(*root);
}

Result<Post> Api::postDetail(std::string_view detailParams) {
    auto body = openBody();
    body.field("browseId", parser::kPostDetailBrowseId).field("params", detailParams);
    auto root = checked(Endpoint::Browse, ClientKind::Mweb, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    auto page = parser::posts(*root);
    if (page.posts.empty()) {
        return fail("post: not found");
    }
    auto result = std::move(page.posts.front());
    result.commentsToken = std::move(page.continuation);
    return result;
}

Result<CommentPage> Api::postComments(std::string_view token) {
    auto body = openBody();
    body.field("continuation", token);
    auto root = checked(Endpoint::Browse, ClientKind::Mweb, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    return parser::comments(*root);
}

Result<std::vector<Post>> Api::subscriptionPosts(std::span<const std::string> preferred) {
    std::vector<std::string> channels;
    const auto add = [&](std::string_view id) {
        if (!id.empty() && channels.size() < kPostChannels &&
            std::ranges::find(channels, id) == channels.end()) {
            channels.emplace_back(id);
        }
    };
    for (const auto& id : preferred) {
        add(id);
    }
    if (channels.size() < kPostChannels) {
        if (auto subscribed = feed(Feed::Channels)) {
            for (const auto& item : subscribed->items) {
                add(item.id);
            }
        }
    }

    std::vector<std::vector<Post>> perChannel;
    Error lastError{"posts: no subscribed channel"};
    for (const auto& id : channels) {
        auto page = channelPosts(id);
        if (!page) {
            lastError = std::move(page.error());
            continue;
        }
        if (page->posts.size() > kPostsPerChannel) {
            page->posts.resize(kPostsPerChannel);
        }
        perChannel.push_back(std::move(page->posts));
    }
    if (perChannel.empty()) {
        return std::unexpected(std::move(lastError));
    }
    // Round robin: every channel's newest post first, then the second ones, and so on.
    std::vector<Post> result;
    for (std::size_t round = 0; round < kPostsPerChannel; ++round) {
        for (auto& posts : perChannel) {
            if (round < posts.size()) {
                result.push_back(std::move(posts[round]));
            }
        }
    }
    return result;
}

Result<std::vector<Account>> Api::accounts() {
    auto body = openBody();
    body.field("requestType", kAccountsRequestType);
    auto root = checked(Endpoint::Accounts, kMetadataClient, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    return parser::accounts(*root);
}

Result<std::vector<PlaylistOption>> Api::saveOptions(std::string_view videoId) {
    auto body = openBody();
    body.singletonArray("videoIds", videoId);
    auto root = checked(Endpoint::AddToPlaylistOptions, kMetadataClient, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    return parser::saveOptions(*root);
}

Result<void> Api::setSaved(std::string_view playlistId, std::string_view videoId, bool saved) {
    auto body = openBody();
    body.field("playlistId", playlistId).beginArray("actions").beginObject();
    body.field("action", saved ? kAddVideoAction : kRemoveVideoAction)
        .field(saved ? "addedVideoId" : "removedVideoId", videoId)
        .endObject()
        .endArray();
    auto root = checked(Endpoint::EditPlaylist, kMetadataClient, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    if (json::string(*root, "/status") != kSucceeded) {
        return fail("edit_playlist: " + std::string(json::string(*root, "/status")));
    }
    return {};
}

Result<std::string> Api::createPlaylist(std::string_view title, Privacy privacy,
                                        std::string_view videoId) {
    auto body = openBody();
    body.field("title", title).field("privacyStatus", kPrivacyNames[std::to_underlying(privacy)]);
    body.singletonArray("videoIds", videoId);
    auto root = checked(Endpoint::CreatePlaylist, kMetadataClient, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    const auto id = json::string(*root, "/playlistId");
    if (id.empty()) {
        return fail("playlist/create: no playlist id");
    }
    return std::string(id);
}

Result<void> Api::rate(std::string_view videoId, Rating rating) {
    auto body = openBody();
    body.beginObject("target").field("videoId", videoId).endObject();
    auto root =
        checked(kRatingEndpoints[std::to_underlying(rating)], kMetadataClient, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    return {};
}

Result<void> Api::setSubscribed(const ChannelSummary& channel, bool subscribed) {
    auto body = openBody();
    body.singletonArray("channelIds", channel.id);
    if (subscribed && !channel.subscribeParams.empty()) {
        body.field("params", channel.subscribeParams);
    }
    auto root = checked(subscribed ? Endpoint::Subscribe : Endpoint::Unsubscribe, kMetadataClient,
                        std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    return {};
}

Result<void> Api::rateComment(std::string_view videoId, std::string_view commentId, Rating rating) {
    using namespace comment_params;
    protobuf::Writer action;
    action.varint(kActionType, kActionTypes[std::to_underlying(rating)])
        .varint(kActionSurface, kActionSurfaceValue)
        .bytes(kActionCommentId, commentId)
        .bytes(kActionVideoId, videoId);
    auto body = openBody();
    body.singletonArray("actions", protobuf::base64Url(action.data()));
    auto root = checked(Endpoint::CommentAction, kMetadataClient, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    if (json::string(*root, "/actionResults/0/status") == kFailedAction) {
        return fail("comment: " +
                    std::string(json::string(*root, "/actionResults/0/feedbackText/runs/0/text")));
    }
    return {};
}

Result<void> Api::postComment(std::string_view videoId, std::string_view text) {
    using namespace comment_params;
    protobuf::Writer params;
    params.bytes(kVideoId, videoId)
        .message(kOptions, protobuf::Writer().varint(kOptionsIndex, 0))
        .varint(kVariant, kVariantValue);
    auto body = openBody();
    body.field("createCommentParams", protobuf::base64Url(params.data()))
        .field("commentText", text);
    auto root = checked(Endpoint::CreateComment, kMetadataClient, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    return {};
}

Result<void> Api::reply(std::string_view videoId, std::string_view commentId,
                        std::string_view text) {
    using namespace comment_params;
    protobuf::Writer params;
    params.bytes(kVideoId, videoId)
        .bytes(kCommentId, commentId)
        .message(kOptions, protobuf::Writer().varint(kOptionsIndex, 0))
        .varint(kVariant, kVariantValue);
    auto body = openBody();
    body.field("createReplyParams", protobuf::base64Url(params.data())).field("commentText", text);
    auto root = checked(Endpoint::CreateReply, kMetadataClient, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    return {};
}

Result<Page> Api::continuation(const Page& page) {
    if (page.source == Endpoint::ReelSequence) {
        return reelSequence(page.continuation);
    }
    auto body = openBody();
    body.field("continuation", page.continuation);
    return pageRequest(page.source, std::move(body));
}

Result<Player> Api::player(std::string_view videoId) {
    std::string failures;
    for (const auto client : playerFallbackOrder()) {
        auto body = openBody();
        body.field("videoId", videoId).field("contentCheckOk", true).field("racyCheckOk", true);
        auto root = post(Endpoint::Player, client, std::move(body));
        if (!root) {
            return std::unexpected(root.error());
        }
        auto parsed = parser::player(*root);
        if (parsed) {
            return parsed;
        }
        failures += clientProfile(client).name;
        failures += ": ";
        failures += parsed.error().message;
        failures += "; ";
    }
    return fail("player: " + failures);
}

Result<Captions> Api::captions(std::string_view videoId) {
    auto body = openBody();
    body.field("videoId", videoId).field("contentCheckOk", true).field("racyCheckOk", true);
    auto root = post(Endpoint::Player, kCaptionsClient, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    return parser::captions(*root);
}

Result<Page> Api::browse(std::string_view browseId) {
    auto body = openBody();
    body.field("browseId", browseId);
    return pageRequest(Endpoint::Browse, std::move(body));
}

Result<Page> Api::pageRequest(Endpoint endpoint, json::Writer body) {
    auto root = checked(endpoint, kMetadataClient, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    auto page = parser::page(*root);
    page.source = endpoint;
    return page;
}

Result<json::Element> Api::checked(Endpoint endpoint, ClientKind client, json::Writer body) {
    auto root = post(endpoint, client, std::move(body));
    if (!root) {
        return std::unexpected(root.error());
    }
    if (const auto message = parser::errorMessage(*root); !message.empty()) {
        return fail("innertube: " + std::string(message));
    }
    return root;
}

Result<json::Element> Api::post(Endpoint endpoint, ClientKind client, json::Writer body) {
    const auto& profile = clientProfile(client);
    std::string visitorData;
    {
        const std::scoped_lock lock(m_visitorMutex);
        visitorData = m_visitorData;
    }

    body.beginObject("context").beginObject("client");
    body.field("clientName", profile.name)
        .field("clientVersion", profile.version)
        .field("hl", m_locale.language)
        .field("gl", m_locale.region);
    if (!profile.deviceMake.empty()) {
        body.field("deviceMake", profile.deviceMake).field("deviceModel", profile.deviceModel);
    }
    if (!profile.osName.empty()) {
        body.field("osName", profile.osName).field("osVersion", profile.osVersion);
    }
    if (profile.androidSdkVersion != 0) {
        body.field("androidSdkVersion", std::int64_t{profile.androidSdkVersion});
    }
    if (!visitorData.empty()) {
        body.field("visitorData", visitorData);
    }
    body.endObject().endObject().endObject();

    http::Request request{
        .method = http::Method::Post,
        .url = std::string(kBaseUrl) + std::string(kEndpointPaths[std::to_underlying(endpoint)]) +
               std::string(kQuery),
        .headers =
            {
                std::string(http::kJsonContentType),
                "User-Agent: " + userAgent(client),
                "X-YouTube-Client-Name: " + std::to_string(profile.id),
                "X-YouTube-Client-Version: " + std::string(profile.version),
            },
        .body = body.take(),
        .retryOnStall = isRead(endpoint),
    };
    if (!visitorData.empty()) {
        request.headers.push_back("X-Goog-Visitor-Id: " + visitorData);
    }
    if (profile.acceptsOAuth && m_session.signedIn()) {
        auto authorization = m_session.authorization();
        if (!authorization) {
            return std::unexpected(authorization.error());
        }
        request.headers.push_back("Authorization: " + *authorization);
    }

    auto response = m_http.send(request);
    if (!response) {
        return std::unexpected(response.error());
    }
    auto root = json::parse(response->body);
    if (!root) {
        return fail("innertube: HTTP " + std::to_string(response->status) + ", " +
                    root.error().message);
    }

    // Remember the visitor identity the server assigns; keeping it stable avoids bot checks.
    if (visitorData.empty()) {
        if (const auto assigned = parser::visitorData(*root); !assigned.empty()) {
            const std::scoped_lock lock(m_visitorMutex);
            m_visitorData = assigned;
        }
    }
    return root;
}

void appendPage(Page& page, Page next) {
    const auto present = [&](const Item& item) {
        return std::ranges::any_of(page.items, [&](const Item& existing) {
            return existing.kind == item.kind && existing.id == item.id;
        });
    };
    std::erase_if(next.items, present);
    page.continuation = next.items.empty() ? std::string() : std::move(next.continuation);
    page.items.insert(page.items.end(), std::make_move_iterator(next.items.begin()),
                      std::make_move_iterator(next.items.end()));
}

} // namespace sloptube::innertube
