#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sloptube::innertube {

enum class Endpoint : std::uint8_t {
    Browse,
    Next,
    Player,
    Search,
    Like,
    Dislike,
    RemoveLike,
    Subscribe,
    Unsubscribe,
    CreateComment,
    CreateReply,
    CommentAction,
    Accounts,
    AddToPlaylistOptions,
    EditPlaylist,
    CreatePlaylist,
    ReelSequence,
    ChannelTab, // `browse` through MWEB: a channel's videos, shorts, live or playlists tab.
    LiveChat,
    ResolveUrl,
};

enum class ItemKind : std::uint8_t {
    Video,
    Short,
    Live,
    Channel,
    Playlist,
};

// A single entry of any feed, search result or playlist.
// Visibility of a playlist, as the server spells it.
enum class Privacy : std::uint8_t {
    Private,
    Unlisted,
    Public,
};
inline constexpr std::array<std::string_view, 3> kPrivacyNames{"PRIVATE", "UNLISTED", "PUBLIC"};

// A playlist offered by "Save", with whether it already holds the video.
struct PlaylistOption {
    std::string id; // "WL" for Watch later.
    std::string title;
    Privacy privacy = Privacy::Private;
    bool contains = false;
};

struct Item {
    ItemKind kind = ItemKind::Video;
    std::string id; // Video, channel or playlist id depending on `kind`.
    std::string title;
    std::string channel;
    std::string detail; // Views, date or video count, already localised by the server.
    std::string badge;  // Duration or "LIVE" overlay.
    std::string thumbnail;
    std::string channelId; // Uploader, when the renderer links to it.
    std::string avatar;    // Uploader avatar, when the renderer carries one.
};

// Search filters, as the server's own filter menu offers them.
enum class UploadDate : std::uint8_t {
    Any,
    LastHour,
    Today,
    ThisWeek,
    ThisMonth,
    ThisYear,
};

enum class ResultType : std::uint8_t {
    Any,
    Video,
    Short,
    Channel,
    Playlist,
    Movie,
};

enum class Duration : std::uint8_t {
    Any,
    UnderThreeMinutes,
    ThreeToTwentyMinutes,
    OverTwentyMinutes,
};

struct SearchFilters {
    bool popular = false; // Sorted by popularity rather than relevance.
    UploadDate date = UploadDate::Any;
    ResultType type = ResultType::Any;
    Duration duration = Duration::Any;
    bool live = false;
    bool hd = false;
    bool fourK = false;
    bool subtitles = false;

    [[nodiscard]] bool operator==(const SearchFilters&) const = default;
};

// The tabs of a channel page besides its home.
enum class ChannelTab : std::uint8_t {
    Videos,
    Shorts,
    Live,
    Playlists,
};

// Mixes ("Radio" in some languages): endless playlists YouTube makes from a video, a channel or a
// genre, whose ids start with "RD". They cannot be browsed like playlists; they play as a queue
// (Api::mix).
[[nodiscard]] constexpr bool isMix(std::string_view playlistId) {
    return playlistId.starts_with("RD");
}

struct Page {
    std::vector<Item> items;
    std::string continuation; // Empty when there are no further pages.
    Endpoint source = Endpoint::Browse;
};

struct Format {
    int itag = 0;
    std::string url;
    std::string mimeType;
    int width = 0;
    int height = 0;
    int fps = 0;
    std::int64_t bitrate = 0;
    std::int64_t contentLength = 0;
    std::string qualityLabel; // Server-side name such as "720p60"; empty for audio.
    bool drc = false;         // Audio with dynamic range compression applied.
    bool defaultAudio = true; // Original language among dubbed audio tracks.
    // Live streams: media time of one segment, fetched with "&sq=<number>"; 0 for files.
    int segmentSeconds = 0;
    int dvrSeconds = 0; // Live: how far behind the edge segments can be played.

    [[nodiscard]] bool hasVideo() const { return width > 0; }

    [[nodiscard]] bool segmented() const { return segmentSeconds > 0; }

    [[nodiscard]] bool hasAudio() const {
        return mimeType.starts_with("audio/") || mimeType.find("mp4a") != std::string::npos;
    }
};

struct VideoDetails {
    std::string id;
    std::string title;
    std::string channel;
    std::string channelId;
    std::int64_t durationSeconds = 0;
    std::int64_t viewCount = 0;
    std::string description;
    bool live = false; // Broadcasting now: no duration, streams are segments of the live edge.
};

// Seek previews: sprite sheets of frames sampled through the video, at several sizes.
struct StoryboardLevel {
    int width = 0; // Of one frame, in pixels.
    int height = 0;
    int count = 0; // Frames in the whole video.
    int columns = 0;
    int rows = 0;
    std::int64_t intervalMs = 0; // Between frames; 0 spreads `count` over the duration.
    std::string url;             // Sheet address; "$M" stands for the sheet number.
};

struct Storyboard {
    std::vector<StoryboardLevel> levels; // Smallest frames first.
};

struct Player {
    VideoDetails details;
    std::vector<Format> formats;
    Storyboard storyboard;
    std::string playbackTrackingUrl; // Reporting this marks the video as watched in history.
    std::string watchtimeTrackingUrl;
};

// A subtitle track of a video, fetched from the timedtext service.
struct CaptionTrack {
    std::string url;      // timedtext address, without a format.
    std::string language; // BCP 47 code, e.g. "en" or "pt-BR".
    std::string name;     // Localised by the server, e.g. "English (auto-generated)".
    bool autoGenerated = false;
    bool translatable = false;
};

// Language the server can machine-translate translatable tracks into.
struct CaptionLanguage {
    std::string code;
    std::string name; // Localised.
};

struct Captions {
    std::vector<CaptionTrack> tracks;
    std::vector<CaptionLanguage> translations;
};

// A user's vote on a video or a comment.
enum class Rating : std::uint8_t {
    None,
    Like,
    Dislike,
};

// Rating after the user presses `pressed`: pressing the current rating clears it.
[[nodiscard]] constexpr Rating toggled(Rating current, Rating pressed) {
    return current == pressed ? Rating::None : pressed;
}

struct ChannelSummary {
    std::string id;
    std::string name;
    std::string handle; // "@name", when known.
    std::string avatar;
    std::string subscribers; // Localised, e.g. "1.25M subscribers".
    bool subscribed = false;
    bool canSubscribe = false; // False for the signed-in user's own channel or when signed out.
    std::string subscribeParams;
};

// Everything the watch page shows besides the stream itself.
struct Chapter {
    std::string title;
    std::int64_t startMs = 0;
};

struct Watch {
    std::string videoId;
    std::string title;
    std::string views; // Localised short count, e.g. "23M views".
    std::string date;  // Localised relative or absolute publication date.
    std::string description;
    ChannelSummary channel;
    Rating rating = Rating::None;
    std::string likes; // Localised like count, empty when hidden.
    bool likesAllowed = true;
    std::string commentCount; // Localised, empty when comments are off.
    std::string commentsToken;
    std::string newestCommentsToken;
    std::vector<Chapter> chapters; // In order; empty when the video has none.
    Page related;
};

struct Comment {
    std::string id;
    std::string author;
    std::string avatar;
    std::string text;
    std::string published;
    std::string likes; // Localised, empty when zero.
    Rating rating = Rating::None;
    std::int64_t replyCount = 0;
    std::string repliesToken;
};

struct CommentPage {
    std::vector<Comment> comments;
    std::string continuation;
};

struct Shelf {
    std::string title;
    std::vector<Item> items;
};

struct Channel {
    ChannelSummary summary;
    std::string banner;
    std::vector<Shelf> shelves;
};

// A YouTube identity available to the signed-in Google account.
struct Account {
    std::string name;
    std::string byline; // E-mail address.
    std::string handle;
    std::string photo;
};

struct PostImage {
    std::string url;
    int width = 0; // Pixels of `url`, for the aspect ratio; 0 when unknown.
    int height = 0;
};

// A community post. Posts are read anonymously (MWEB), so the user's own vote is unknown.
struct Post {
    std::string id;
    std::string channelId;
    std::string author;
    std::string avatar;
    std::string published; // Localised relative date.
    std::string text;
    std::vector<PostImage> images;    // One image, or the pages of a carousel.
    std::vector<std::string> choices; // Poll or quiz answers.
    std::string votes;                // Localised poll total, e.g. "38 k votes".
    std::optional<Item> video;        // Attached video.
    std::string likes;                // Localised, empty when zero.
    std::string commentCount;         // Localised, empty when comments are off.
    std::string detailParams;         // Browse params of the post's own page.
    std::string commentsToken;        // Only known from the post's own page.
};

struct PostPage {
    std::vector<Post> posts;
    std::string continuation;
};

// A message of a live stream's chat.
struct ChatMessage {
    std::string id;
    std::string author; // "@handle".
    std::string avatar;
    std::string text;   // Emoji as characters; channel emoji as their ":shortcut:".
    std::string amount; // Super Chat amount, localised; empty for an ordinary message.
};

struct ChatPage {
    std::vector<ChatMessage> messages; // Oldest first.
    std::string continuation;          // Empty once the chat is over.
    int pollMs = 0;                    // When to ask for the next page.
};

} // namespace sloptube::innertube
