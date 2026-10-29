# InnerTube API

InnerTube is the JSON API behind every official YouTube client. All requests are
`POST https://youtubei.googleapis.com/youtubei/v1/<endpoint>?prettyPrint=false` with a body
holding the endpoint fields plus a `context.client` object identifying the client. The code is
in `src/core/innertube/`.

## Client profiles

`ClientProfile.cpp` is the only place client names, ids, versions and user agents are written.

| Kind | Name / id | Used for | Why |
| --- | --- | --- | --- |
| `Tv` | `TVHTML5` / 7 | Every metadata request and every account action | Accepts the OAuth token from the device flow; compact responses |
| `VisionOs` | `VISIONOS` / 101 | Stream URLs (first choice) | Anonymous; the only client whose adaptive streams download past the first minute (needs `visitorData`) |
| `Android` | `ANDROID` / 3 | Stream URLs (fallback) | Anonymous; its progressive itag 18 is complete |
| `Ios` | `IOS` / 5 | Not used for playback | Its streams stop after about a minute |
| `Mweb` | `MWEB` / 2 | Uploader and avatar lookups | Anonymous, and has small responses for both |

User agents are templates (`{version}`, `{model}`, `{os}`) expanded once from the profile's own
fields, so a version bump is a one-line change.

Requests go over IPv4 when there is a route: on the target phone's Wi-Fi the same search took
2.8-5.5 s over IPv6 against 1.1-1.8 s over IPv4 (`http::Client`). Reads (`browse`, `next`,
`player`, `search`, account and playlist lists) are sent again once, on a fresh connection, when
the server stays silent for 5 s (`Request::retryOnStall`); actions never are, so a like or a
comment cannot be applied twice.

Requests carry `X-YouTube-Client-Name`, `X-YouTube-Client-Version`, and the `visitorData` the
server assigned on the first response (keeping one visitor identity avoids bot checks).

## Endpoints

| `Api` method | Endpoint | Body | Parser |
| --- | --- | --- | --- |
| `feed(Home/Subscriptions/History/Playlists/Channels)` | `browse` | `browseId` = `FEwhat_to_watch`, `FEsubscriptions`, `FEhistory`, `FEplaylist_aggregation`, `FEchannels` | `page` |
| `playlist(id)` | `browse` | `browseId` = `VL<id>` | `page` |
| `channel(id)` | `browse` | `browseId` = channel id | `channel` |
| `channelAvatar(id)` | `browse` (MWEB) | `browseId` = channel id, `params` = the channel's search tab, empty `query` | `channelAvatar` |
| `videoChannel(videoId)` | `player` (MWEB) | `videoId` | `videoChannel` |
| `search(query, filters)` | `search` | `query`, `params` = the filters (see [Search](#search)) | `page` |
| `searchSuggestions(query)` | not InnerTube: `suggestqueries-clients6.youtube.com/complete/search` | — | JSON array |
| `channelTab(id, tab)` | `browse` (MWEB) | `browseId` = channel id, `params` = the tab's | `channelTab` (see [Channel tabs](#channel-tabs)) |
| `liveChat(token)` | `live_chat/get_live_chat` | `continuation` | `liveChat` (see [Live chat](#live-chat)) |
| `shorts()` | `search` | empty `query`, `params` = `EgIQCQ==` (type: Shorts) | `page`, shorts only (see [Shorts feed](#shorts-feed)) |
| `continuation(shorts page)` | `reel/reel_watch_sequence` (MWEB), then `player` (MWEB) per short | `sequenceParams` | `reelSequence`, `videoDetails` |
| `continuation(page)` | same endpoint as the page | `continuation` | `page`, appended with `appendPage` |
| `watch(videoId)` | `next` | `videoId` | `watch` |
| `comments(token)` | `next` | `continuation` | `comments` |
| `player(videoId)` | `player` | `videoId`, `contentCheckOk`, `racyCheckOk` | `player` |
| `storyboard(videoId)` | `player` (ANDROID) | same | `storyboard` |
| `channelPosts(id)` | `browse` (MWEB) | `browseId` = channel id, `params` = the channel's posts tab | `posts` |
| `postDetail(params)` | `browse` (MWEB) | `browseId` = `FEpost_detail`, `params` from the post | `posts` (+ comments token) |
| `postComments(token)` | `browse` (MWEB) | `continuation` | `comments` |
| `subscriptionPosts(channels)` | several of the above | see [Community posts](#community-posts) | — |
| `accounts()` | `account/accounts_list` | `requestType` = `ACCOUNTS_LIST_REQUEST_TYPE_CHANNEL_SWITCHER` | `accounts` |
| `rate(id, Like)` | `like/like` | `target.videoId` | — |
| `rate(id, Dislike)` | `like/dislike` | `target.videoId` | — |
| `rate(id, None)` | `like/removelike` | `target.videoId` | — |
| `setSubscribed(ch, true)` | `subscription/subscribe` | `channelIds[]`, `params` | — |
| `setSubscribed(ch, false)` | `subscription/unsubscribe` | `channelIds[]` | — |
| `postComment(id, text)` | `comment/create_comment` | `createCommentParams`, `commentText` | — |
| `reply(id, comment, text)` | `comment/create_comment_reply` | `createReplyParams`, `commentText` | — |
| `rateComment(id, comment, r)` | `comment/perform_comment_action` | `actions[]` | — |
| `saveOptions(videoId)` | `playlist/get_add_to_playlist` | `videoIds[]` | `saveOptions` (`playlistAddToOptionRenderer`: id, title, privacy, whether it holds the video) |
| `setSaved(playlist, video, true)` | `browse/edit_playlist` | `playlistId`, `actions[]` = `ACTION_ADD_VIDEO` + `addedVideoId` | — |
| `setSaved(playlist, video, false)` | `browse/edit_playlist` | `playlistId`, `actions[]` = `ACTION_REMOVE_VIDEO_BY_VIDEO_ID` + `removedVideoId` | — |
| `createPlaylist(title, privacy, video)` | `playlist/create` | `title`, `privacyStatus` (`PRIVATE`/`UNLISTED`/`PUBLIC`), `videoIds[]` | new playlist id |

### Avatar lookups

Feed avatars are resolved with the cheapest requests found, measured on the live service:

| Request | Response | Time |
| --- | --- | --- |
| Channel home (`browse`, MWEB) | 180–540 KB | ~0.9 s |
| Channel search tab with an empty query (`channelAvatar`) | ~30 KB | ~0.13 s |
| `player` (MWEB), for the uploader of a video (`videoChannel`) | ~13 KB | ~0.1 s |

TV home tiles no longer link to their channel, so their avatars need both lookups, chained.
Searching the TV client for a channel id does not work: it returns unrelated channels.

Responses carrying an `error` object become a failed `Result` with the server's (localised)
message.

## Search

Filters are the `params` of `search`, a protobuf as the web client's filter menu builds it:
field 1 is the sort (3 = popularity; relevance is the default, and the menu no longer offers upload
date or rating), field 2 a message of filters. TVHTML5 honours all of them, including Shorts with a
real query; MWEB ignores some. The params are standard base64 (`protobuf::base64`).

| Filter | Field | Values |
| --- | --- | --- |
| Upload date | 1 | 1 last hour, 2 today, 3 this week, 4 this month, 5 this year |
| Type | 2 | 1 video, 9 short, 2 channel, 3 playlist, 4 movie |
| Duration | 3 | 4 under 3 min, 5 3-20 min, 2 over 20 min |
| Features | 4 HD, 5 subtitles, 8 live, 14 4K | 1 |

Combinations work, and continuations keep the filters. Suggestions come from the service the
official apps use, in plain JSON with `xhr=t` (`["query", [["suggestion", 0, [...]], ...], {...}]`),
in the app's `hl`/`gl`, in about 0.1 s.

## Channel tabs

A channel's Videos, Shorts, Live and Playlists tabs are read anonymously through MWEB `browse`
with fixed `params`, the same for every channel (`kChannelTabParams`). Asked for a tab it does not
have, the server answers with the home tab, so `parser::channelTab` checks that the selected tab's
URL ends with the tab's path (`/videos`, `/shorts`, `/streams`, `/playlists`) and returns nothing
otherwise.

| Tab | Items | Renderer |
| --- | --- | --- |
| Videos, Live | `richGridRenderer` → `richItemRenderer` | `videoWithContextRenderer`: `headline`, `lengthText`, `shortViewCountText`, `publishedTimeText`; a `thumbnailOverlayTimeStatusRenderer` of style `LIVE` marks a broadcast |
| Shorts | same | `shortsLockupViewModel`: id from `reelWatchEndpoint`, views from `overlayMetadata`; the title only exists in `accessibilityText` ("title, views - action"), cut at its last separators |
| Playlists | `sectionListRenderer` → `itemSectionRenderer` | `compactPlaylistRenderer`: `playlistId`, `title`, `videoCountShortText` |

The next page's token is the `continuationItemRenderer` ending the list (header chips and the
about panel carry other continuations, which are skipped); continuations go to the same endpoint
(`Endpoint::ChannelTab`) and come back as `appendContinuationItemsAction`. Pages hold 30 items
(48 shorts), 60-340 KB, in about 0.2 s. They do not name the channel; the app fills it in.

## Live chat

Only the WEB `next` response carries a live chat's token, at about 760 KB, so the app builds it
from the channel and video ids (`Api::liveChatToken`, a protobuf whose fields decode the same as
the web client's token), then polls `live_chat/get_live_chat` with the TV client, anonymously
(MWEB and VISIONOS are refused). The first answer holds the latest 50-70 messages (about 100 KB),
the next ones what arrived since (a few KB), every 3-5 s as `timedContinuationData.timeoutMs` says.

`parser::liveChat` keeps `liveChatTextMessageRenderer` (author, photo, text runs, where a Unicode
emoji run's `emojiId` is the character itself and a channel emoji shows as its shortcut) and
`liveChatPaidMessageRenderer` (with `purchaseAmountText`); engagement notices, gifts and
memberships are skipped.

## Renderers understood

| Renderer | Where | Becomes |
| --- | --- | --- |
| `tileRenderer` | TV feeds, related videos, channel shelves | `Item` (with `channelId` from the tile menu) |
| `lockupViewModel` | Search results | `Item` (with avatar from `decoratedAvatarViewModel`) |
| `nextContinuationData` | Everywhere | page continuation (shelf-level pagination inside `horizontalListRenderer` is ignored) |
| `adSlotRenderer`, `adSlotAndLayoutRenderer` | Feeds | skipped entirely |
| `videoMetadataRenderer`, `videoOwnerRenderer`, `subscribeButtonRenderer` | `next` | `Watch` title, views, date, `ChannelSummary` |
| `likeButtonRenderer` | `next` | rating, like count |
| `expandableVideoDescriptionBodyRenderer` | `next` | description |
| `engagementPanelSectionListRenderer` (`comment-item-section`) | `next` | comment count, tokens for "Top" and "Newest" |
| `macroMarkersListEntity` (`MARKER_TYPE_CHAPTERS`) | `next` | `Watch::chapters` (title, start) |
| `commentRenderer` | `next` continuation | `Comment` (author, avatar, text, likes, vote, replies token) |
| `channelHeaderRenderer`, `shelfRenderer` | channel `browse` | `Channel` (banner, avatar, handle, subscribers, shelves) |
| `accountItem` | `accounts_list` | `Account` |
| `backstagePostRenderer` | MWEB posts tab, `FEpost_detail` | `Post` (author, date, text, images, poll, video, likes, comment count, page params) |

`FEplaylist_aggregation` answers every continuation with its first page again. `appendPage`
drops items already in the list and ends the feed when a page brings nothing new, so the
account's playlists do not repeat forever.

Text fields come in three encodings (`simpleText`, `runs[]`, `content`); `parser::text()`
flattens all of them.

## Community posts

The TV client has no posts at all (a channel's posts tab and `FEpost_detail` come back empty),
and no personalised source of posts accepts the OAuth token, so posts are read anonymously
through MWEB, which serves the same `backstagePostRenderer`s as the web site:

| Request | Response | Time |
| --- | --- | --- |
| Subscribed channels, `FEchannels` (TV) | ~13 KB | ~0.15 s |
| A channel's posts tab (MWEB), latest ~10 posts | 20–150 KB | ~0.2 s |
| A post's own page (MWEB) | ~17 KB | ~0.13 s |
| A page of post comments (MWEB) | ~40 KB | ~0.3 s |

`subscriptionPosts` picks up to 6 channels — first the uploaders of the subscriptions feed, which
is ordered by latest activity, then the rest of `FEchannels` (alphabetical) — fetches their posts
tabs, keeps the newest 3 posts of each and interleaves them channel by channel (about 2 s in
total, once, in the background). The WEB client gives the same posts in responses about twice as
large.

Parsed attachments: `backstageImageRenderer` (one image; the smallest rendition at least 640 px
wide), `postMultiImageRenderer` (carousel), `pollRenderer` and `quizViewModel` (answers and vote
total; anonymous responses carry no percentages), `videoRenderer`/`compactVideoRenderer`. Author
avatars are protocol-relative (`//yt3…`) and get `https:`.

Post comments are `commentRenderer`s like the TV ones, but replies hang off the
`commentThreadRenderer` (`commentRepliesRenderer` → `continuationCommand`) and pages continue
through `continuationItemRenderer`; `parser::comments` handles both shapes.

**Not supported:** voting on posts and polls, and rating or answering post comments. The user's
own vote is unknown (anonymous reads), and `like/like` with `target.postId` is answered with
success whatever the target (even a made-up id) without changing the public count, so the app
shows a toast instead of pretending.

## Mixes

Feeds and suggestions carry mixes: playlists YouTube makes from a video, a channel or a genre
(ids starting with `RD`; "Radio inspirée du titre", "Radios de genre" in French). They cannot be
browsed as playlists (`browse VL<id>` answers "invalid argument"). `next` asked with the mix's
`playlistId` alone starts it instead: the first shelf of its suggestions ("Up next") is the
queue, 20 videos from the one playing (`Api::mix`, `parser::mixQueue`). The app plays the first
with that queue as the list it came from, so "next" and autoplay go through the mix.

## Shorts feed

The TV client ignores the Shorts filter of `search`: with an empty query it returns one page of
personalised results mixing long videos (15–25 min) and a few shorts, without continuation.
`shorts()` keeps only the shorts, and the first one seeds `reel/reel_watch_sequence`, the
endless sequence behind the official Shorts pager. Its parameters are a protobuf built in
`reelSequenceParams`: `1` the seed video id, `5` `{3: 5}`, `10` 25, `13` 0. MWEB answers it
anonymously with about 8 shorts per page (~20 KB) and a continuation token; WEB gives the same
ids in responses 10 to 50 times larger.

Sequence entries carry the video id and a 1080×1920 poster but no title, so each short is
completed by an MWEB `player` (`videoDetails`: title, uploader, uploader id). A page costs
about 2–3 s on a worker, fetched ahead of the pager. Measured: 3 pages, 42 shorts, no duplicate.

## Chapters

The TV `next` response carries a video's chapters in `frameworkUpdates.entityBatchUpdate`: a
mutation whose `macroMarkersListEntity.markersList` has `markerType` `MARKER_TYPE_CHAPTERS` and
one marker per chapter (`title`, `startMillis` as a string, `durationMillis`). Lists of other
marker types (key moments) are ignored. Videos without chapters have no such list.

## Comment parameters

Comment writes take opaque base64url protobuf parameters, built with `core/util/Protobuf.hpp`:

| Message | Fields |
| --- | --- |
| create comment | `2`: video id, `5`: `{1: 0}`, `10`: 7 |
| create reply | `2`: video id, `4`: parent comment id, `5`: `{1: 0}`, `10`: 7 |
| comment action | `1`: action type, `2`: 2, `3`: comment id, `5`: video id |

Action types: 5 = like, 4 = dislike, 6 = remove like. The TV backend accepts like and dislike;
clearing a vote is answered with `STATUS_FAILED`, in which case the app restores the previous
state and shows a toast.

## Storyboards

Seek previews come from `playerStoryboardSpecRenderer.spec`, which only the ANDROID and IOS
clients send (VISIONOS, MWEB and TV do not), hence the separate `storyboard` request. The spec is
`<base url>|<level>|<level>…`, each level `width#height#count#columns#rows#intervalMs#name#sigh`.
In the base URL `$L` is the level index and `$N` its name; `$M` is the sheet number, and the
sheet URL gets `&sigh=`. `storyboardFrame` (`Storyboard.hpp`) picks the smallest level at least
as wide as asked and returns the sheet URL and the frame's rectangle in it. A typical video has
48×27 (one sheet), 80×45 (10×10 sheets) and 160×90 (5×5 sheets, one frame every 2 s); the app
uses the largest.

## Streams

See [media.md](media.md), and [Live streams](media.md#live-streams) for broadcasts in progress. Only formats with a direct `url` are used; ciphered or SABR-only
formats are skipped.
