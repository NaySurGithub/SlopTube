#pragma once

#include "core/innertube/Types.hpp"
#include "core/json/Json.hpp"
#include "core/util/Result.hpp"

#include <optional>
#include <string_view>

// Converts InnerTube responses into the client-agnostic model. Understands both the TV "tile"
// renderers and the newer "lockup" view models.
namespace sloptube::innertube::parser {

[[nodiscard]] Page page(json::Element root);

// Seek-preview sheets from a player response's storyboard spec.
[[nodiscard]] Storyboard storyboard(std::string_view spec);
[[nodiscard]] Result<Player> player(json::Element root);

// Subtitle tracks listed by a `player` response; empty when the client is not given any.
[[nodiscard]] Captions captions(json::Element root);

// `next` response of a video: metadata, channel, rating, comment entry points and related items.
[[nodiscard]] Watch watch(json::Element root);

// `next` continuation listing comments or the replies to one.
[[nodiscard]] CommentPage comments(json::Element root);

[[nodiscard]] Channel channel(json::Element root);

[[nodiscard]] std::vector<Account> accounts(json::Element root);

// `playlist/get_add_to_playlist`: the playlists "Save" offers.
[[nodiscard]] std::vector<PlaylistOption> saveOptions(json::Element root);

// Browse id of a post's own page (comments included); its params come with each post.
inline constexpr std::string_view kPostDetailBrowseId = "FEpost_detail";

// Community posts of a channel's posts tab or of a post's own page (MWEB `backstagePostRenderer`).
// The continuation is the next posts page, or the first comments page on a post's own page.
[[nodiscard]] PostPage posts(json::Element root);

// Channel avatar from any channel page, whatever the client's header renderer.
[[nodiscard]] std::string channelAvatar(json::Element root);

// Uploader channel id from a `player` response.
[[nodiscard]] std::string_view videoChannel(json::Element root);

// Title, uploader and uploader id from a `player` response, into `item`.
void videoDetails(json::Element root, Item& item);

// Shorts of a `reel/reel_watch_sequence` page: id and poster only, and the next page's token.
[[nodiscard]] Page reelSequence(json::Element root);

// A page of a channel tab (MWEB `browse`), or a continuation of one when `tab` is absent. The
// first page is empty when the server answered with another tab: the channel has no `tab`.
[[nodiscard]] Page channelTab(json::Element root, std::optional<ChannelTab> tab);

// `live_chat/get_live_chat`: new messages and when to ask again.
[[nodiscard]] ChatPage liveChat(json::Element root);

[[nodiscard]] std::string_view visitorData(json::Element root);

// Message of an `error` object returned instead of a regular response, if any.
[[nodiscard]] std::string_view errorMessage(json::Element root);

} // namespace sloptube::innertube::parser
