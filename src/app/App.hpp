#pragma once

#include "app/Settings.hpp"
#include "app/downloads/Downloads.hpp"
#include "app/media/Player.hpp"
#include "app/media/VideoSurface.hpp"
#include "app/platform/Bridge.hpp"
#include "app/platform/Display.hpp"
#include "app/platform/Platform.hpp"
#include "app/ui/Avatars.hpp"
#include "app/ui/PostCard.hpp"
#include "app/ui/SeekUndo.hpp"
#include "app/ui/Subtitles.hpp"
#include "app/ui/Thumbnails.hpp"
#include "app/ui/Touch.hpp"
#include "app/ui/VectorIcons.hpp"
#include "app/ui/Views.hpp"
#include "app/updates/Updater.hpp"
#include "app/util/TaskQueue.hpp"
#include "core/auth/Session.hpp"
#include "core/http/Client.hpp"
#include "core/innertube/Api.hpp"
#include "core/innertube/Storyboard.hpp"
#include "core/innertube/Streams.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

struct android_app;
struct AInputEvent;
struct ANativeWindow;
struct ImFont;

namespace sloptube::app {

enum class Tab : std::uint8_t {
    Home,
    Shorts,
    Subscriptions,
    You,
};

inline constexpr std::size_t kTabCount = 4;

enum class Route : std::uint8_t {
    Search,
    Watch,
    Playlist,
    History,
    SignIn,
    Channel,
    Post,
    Downloads,
    Settings,
    Notifications,
    AllSubscriptions, // Every subscribed channel ("All" in Subscriptions).
};

// Panels sliding up over part of the screen.
enum class Sheet : std::uint8_t {
    None,
    Comments,
    Description,
    Accounts,
    Save,
    Download,
    LiveChat,
    Update,      // A newer release of the app is offered.
    ItemActions, // The ⋮ / ••• menu of a video.
};

// A paginated list of comments: a video's top-level comments or the replies to one.
struct CommentList {
    std::vector<innertube::Comment> comments;
    std::vector<float> heights; // Measured when last drawn, for positioning off-screen rows.
    std::vector<bool> expanded;
    std::string continuation;
    bool loading = false;
    bool loaded = false;
    std::string error;
    Scroller scroller;
    // Comments of a community post: read anonymously, so they cannot be rated or answered.
    bool postComments = false;
};

// A community post's own page: the post above its comments, or the replies to one of them.
struct PostScreen {
    PostCard card;
    std::shared_ptr<CommentList> comments = std::make_shared<CommentList>();
    std::shared_ptr<CommentList> replies;
};

// Posts of subscribed channels interleaved with the home feed, loaded once after it.
struct PostFeed {
    std::vector<PostCard> cards;
    bool requested = false;
};

struct ChannelPage {
    innertube::Channel channel;
    std::vector<std::shared_ptr<Feed>> shelves; // One horizontal feed per channel shelf.
    bool loading = true;
    std::string error;
    Scroller scroller;
    // The tab shown under the header; none is the home tab and its shelves.
    std::optional<innertube::ChannelTab> tab;
    std::array<std::shared_ptr<Feed>, 4> tabs; // Loaded when first shown.
    float headerHeight = 0;                    // Measured on the previous frame.
    Scroller tabScroller{Axis::Horizontal};
    float tabsWidth = 0; // Of the chip row, measured on the previous frame.
};

// The chat of the live stream being watched, polled while it is shown.
struct LiveChat {
    std::vector<innertube::ChatMessage> messages; // Oldest first, the newest few hundred.
    std::string token;
    double nextPollAt = 0;
    bool polling = false;
    bool failed = false;
    Scroller scroller;
    bool following = true;      // Scrolled to the newest message, so new ones scroll it on.
    std::vector<float> heights; // Of the first messages, measured at measuredWidth.
    float measuredWidth = 0;
};

// Panels opened from the player's controls.
enum class PlayerMenu : std::uint8_t {
    None,
    Settings, // Quality and playback speed.
    Quality,
    Speed,
    Subtitles,
    Translations, // Languages to machine-translate subtitles into.
};

// The list a video was opened from, which "next" continues.
struct VideoSource {
    std::shared_ptr<Feed> feed;
    std::size_t index = 0;
};

struct Screen {
    Route route;
    std::string argument; // Search query, playlist, video or channel id.
    std::string title;
    std::shared_ptr<Feed> feed = std::make_shared<Feed>();
    std::shared_ptr<ChannelPage> channel;
    std::shared_ptr<PostScreen> post;
    double openedAt = 0; // For the entry transition.
};

// The watch page shrunk into a floating window that keeps playing over the other screens.
// Launch animation: the logo turning into a loading bar until the home feed is in.
struct SplashState {
    double startedAt = -1; // First frame.
    double barAt = -1;     // The feed was not in yet: the logo turns into the loader.
    double doneAt = -1;    // The feed is in (and the bar full); the splash fades out.
    float progress = 0;
    bool finished = false;
};

struct MiniPlayerState {
    std::optional<Screen> watch; // The minimised watch page; set while the window is shown.
    float pull = 0;              // px the watch page is being dragged down by, towards minimising.
    std::size_t corner = 3;      // Index into the window's corners (bottom right by default).
    ImVec2 position{-1, -1};     // Top-left corner (px), animated towards `corner`.
    bool dragging = false;
    ImVec2 grabbed; // `position` when the drag began.
    double minimizedAt = -1;
    ImVec2 fromMin, fromMax; // Where the video was when minimised, to animate from.
    ImVec2 min, max;         // Window drawn on the previous frame, for hit testing.
    bool controls = false;
    double controlsShownAt = 0;
    double lastTapAt = -1; // A second tap soon after restores the watch page.
};

class App {
public:
    explicit App(android_app* native);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    void onWindowCreated(ANativeWindow* window);
    void onWindowDestroyed();
    void onInsetsChanged();
    void onPause();
    // The app left the screen (not for picture-in-picture).
    void onStop();
    std::int32_t onInput(const AInputEvent* event);

    // How long the event loop may sleep while idle: forever, unless something goes on unseen
    // (playback in the background, an autoplay countdown); -1 for forever.
    [[nodiscard]] int idleWakeMs() const;

    // Renders one frame; returns whether another should follow without waiting for events.
    bool frame();
    // Closes a frame interrupted by an exception, so the next one starts cleanly.
    void abandonFrame();

private:
    // A seek in progress, in the official app's three ways: dragging the progress bar, dragging
    // the video after holding it ("easy seek"), or dragging the film strip of precise seeking.
    struct SeekState {
        std::int64_t targetMs = 0;
        std::int64_t fromMs = 0; // Playback position when it began; undo returns there.
        ImVec2 origin{};         // Finger at the start, or where precise seeking last toggled.
        bool easy = false;
        SeekUndo undo;
        std::size_t chapter = 0; // Under the target, to tick when it changes.
    };

    struct WatchState {
        innertube::Item item;
        VideoSource source;
        std::optional<innertube::VideoDetails> details;
        std::string error;
        bool controlsVisible = true;
        double controlsShownAt = 0;
        std::optional<SeekState> seek;    // While a drag seeks.
        bool fine = false;                // Precise seeking open; stays until closed.
        bool fineResume = false;          // Playing when precise seeking opened.
        std::int64_t fineMs = 0;          // Position under the film strip's line.
        innertube::Storyboard storyboard; // Seek previews, empty until loaded.
        // Last preview drawn, kept on screen while the sheet of the next one downloads.
        std::optional<innertube::StoryboardFrame> shownPreview;
        PlayerMenu menu = PlayerMenu::None;
        Scroller menuScroller;

        std::optional<innertube::Watch> info;
        float headerHeight = 0; // Measured on the previous frame.
        double likePopAt = -1;
        bool newestFirst = false;
        std::shared_ptr<CommentList> comments;
        std::shared_ptr<CommentList> replies; // Replies view of the comments sheet, if open.
        std::optional<innertube::Comment> replyParent;
        Scroller descriptionScroller;
        float descriptionHeight = 0; // Measured on the previous frame.
        Scroller actionsScroller{Axis::Horizontal};
        float actionsWidth = 0;         // Measured on the previous frame.
        double endedAt = -1;            // The video ended: the autoplay countdown runs from here.
        bool autoplayCancelled = false; // The countdown was cancelled for this video.
        std::shared_ptr<LiveChat> chat; // Live streams: shown in place of the comments teaser.
    };

    struct SheetState {
        Sheet kind = Sheet::None;
        double openedAt = 0;
        double closingAt = -1; // Set while sliding out.

        // Account and save sheets cover the screen with a scrim and size to their content.
        [[nodiscard]] bool modal() const {
            return kind == Sheet::Accounts || kind == Sheet::Save || kind == Sheet::Download ||
                   kind == Sheet::Update || kind == Sheet::ItemActions;
        }
    };

    // "Save": the signed-in account's playlists offered for one video.
    struct SaveState {
        std::string videoId;
        std::vector<innertube::PlaylistOption> options;
        bool loading = false;
        bool failed = false;
        bool creating = false; // The new-playlist form replaces the list.
        bool submitting = false;
        bool focusTitle = false;
        innertube::Privacy privacy = innertube::Privacy::Private;
        std::array<char, 151> title{}; // YouTube caps playlist titles at 150 characters.
        Scroller scroller;
    };

    // "Download": the qualities offered for one video, or the download already made.
    struct DownloadSheetState {
        innertube::Item item;
        std::optional<innertube::Player> player;
        innertube::Streams streams;
        bool loading = false;
        bool failed = false;
    };

    // Vertical pager of the Shorts tab.
    struct ShortsState {
        std::size_t index = 0; // Short being played.
        float position = 0;    // Scroll position in pages, animated towards `index`.
        std::string playing;   // Video id handed to the player.

        // Responses fetched ahead for the shorts around the current one, oldest first.
        struct Prefetched {
            std::string id;
            std::optional<innertube::Watch> watch;
            std::optional<innertube::Player> player;
        };

        std::vector<Prefetched> prefetched;
        std::string standby;        // Video id preloaded on the player's standby session.
        std::optional<float> scrub; // Fraction while dragging the progress bar.
        double seekedAt = 0;        // Last seek while scrubbing, to pace them.
        float seekedTo = -1;        // Fraction of that seek.
        float scrubFrom = 0;        // Fraction playing when the drag began; undo returns there.
        SeekUndo undo;
        // Double-tap like: a tap waits for a second one before pausing.
        double tapAt = -1;
        double burstAt = -1; // The like animation, where the second tap landed.
        ImVec2 burstCentre{};
    };

    struct Toast {
        std::string text;
        double shownAt = -1;
    };

    struct SignInState {
        std::optional<auth::DeviceCode> code;
        bool failed = false;
        std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
    };

    // Background playback, media controls and picture-in-picture (Background.cpp).
    void onBridgeEvent(BridgeEvent event);
    // Publishes the playing video to the media session and picture-in-picture when it changed.
    void syncPlayback();
    // Plays the next video once one ends: after a countdown on screen, at once otherwise.
    void updateAutoplay();
    void playNext();
    void drawAutoplay(Frame& frame, ImVec2 min, ImVec2 max);
    // Picture-in-picture: only the video, filling the window.
    void drawPictureInPicture(Frame& frame, ImVec2 display);
    // Watching a video: the watch page or the mini player (not Shorts).
    [[nodiscard]] bool watching() const;

    // The ⋮ / ••• menu of a video (ItemActions.cpp).
    void openItemActions(innertube::Item item);
    [[nodiscard]] float itemActionsSheetHeight(const Canvas& canvas) const;
    void drawItemActionsSheet(Frame& frame, ImVec2 min, ImVec2 max);

    // Subscriptions (SubscriptionsPage.cpp): the channels row, the filter chips, the feed.
    void drawSubscriptions(Frame& frame, ImVec2 min, ImVec2 max);
    void filterSubscriptions(int filter);

    // The bell (NotificationsPage.cpp).
    void openNotifications();
    void drawNotifications(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max);
    void loadSeenNotifications();
    void markNotificationsSeen();
    [[nodiscard]] int unreadNotifications();
    static void drawUnreadBadge(const Canvas& c, ImVec2 bell, int count);

    // Settings (SettingsPage.cpp).
    void openSettings();
    void drawSettings(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max);
    void applySettings();
    // Sizes everything for Settings::interfaceSize and Android's text size.
    void applyScale();

    // Search (SearchPage.cpp): suggestions and history while typing, filters over the results.
    void submitSearch(Screen& screen, std::string query);
    // submitSearch on the search screen, if it is still the one shown.
    void searchFor(const std::string& query);
    void requestSuggestions(const std::string& query);
    void drawSearchSuggestions(Frame& frame, ImVec2 min, ImVec2 max);
    [[nodiscard]] float drawSearchFilters(Frame& frame, Screen& screen, ImVec2 origin, float width);
    void rememberSearch(const std::string& query);

    // Live streams (LivePage.cpp): the chat, rewinding, going back to live.
    void openLiveChat();
    void pollLiveChat();
    void drawLiveChat(Frame& frame, ImVec2 min, ImVec2 max);

    // Where a live stream stands, in ms within what can be played; nullopt for other videos.
    struct LiveTimeline {
        std::int64_t durationMs = 0; // From the oldest playable moment to the edge.
        std::int64_t playedMs = 0;
        bool atEdge = true;
    };

    [[nodiscard]] std::optional<LiveTimeline> liveTimeline() const;
    void seekLive(std::int64_t timelineMs);

    // Updates of the app itself (UpdatePage.cpp). `manual`: asked from Settings, so "up to date"
    // is said too.
    void checkForUpdate(bool manual);
    void downloadUpdate();
    void drawUpdateSheet(Frame& frame, ImVec2 min, ImVec2 max);
    [[nodiscard]] float updateSheetHeight(const Canvas& canvas) const;

    // Channel tabs (ChannelPage.cpp).
    void selectChannelTab(ChannelPage& page, std::optional<innertube::ChannelTab> tab);
    [[nodiscard]] float drawChannelHeader(Frame& frame, const std::shared_ptr<ChannelPage>& page,
                                          ImVec2 origin, float width);

    // Navigation.
    [[nodiscard]] bool canGoBack() const;
    void goBack();
    void selectTab(Tab tab);
    void push(Screen screen);
    // Navigation requested while drawing is applied before the next frame, so the screen being
    // drawn is never destroyed mid-draw.
    void later(std::function<void()> action);
    void openItem(innertube::Item item, VideoSource source = {});
    // The video on screen, if any, joins the history "previous" returns to, unless
    // `recordHistory` is false.
    void openVideo(innertube::Item item, VideoSource source = {}, bool recordHistory = true);
    void playPrevious();
    // A mix ("Radio") plays as a queue: its first video, then the others (innertube::isMix).
    void openMix(const std::string& playlistId);
    // The item after the current video in the list it was opened from, else the first related
    // video.
    [[nodiscard]] std::optional<std::pair<innertube::Item, VideoSource>> nextVideo() const;
    void closeVideo();
    // Mini player: the watch page dragged down (or left with Back) keeps playing in a floating
    // window that can be moved to any corner, reopened or closed.
    void minimizeVideo(ImVec2 videoMin, ImVec2 videoMax);
    void restoreVideo();
    void closeMiniPlayer();
    // Tracks the drag-down gesture on the watch page's video; returns the pull in px.
    float pullWatch(Frame& frame, ImVec2 videoMin, ImVec2 videoMax);
    void drawMiniPlayer(Frame& frame, ImVec2 min, ImVec2 max);
    void startPlayback(const std::string& videoId);
    // Fetches the seek previews of the video in m_watch.
    void loadStoryboard(const std::string& videoId);
    // The storyboard frame at `timeMs` over the video in [min, max], dimmed, with the time in a
    // pill above `barY`.
    // Above the pill, the chapter at `timeMs`; with `label`, the pill shows it instead of the time.
    void drawScrubPreview(Frame& frame, ImVec2 min, ImVec2 max, std::int64_t timeMs, float barY,
                          std::string_view label = {});
    // Precise seeking: the film strip centred on m_watch.fineMs, its time, play and close.
    void drawFineSeek(Frame& frame, ImVec2 min, ImVec2 max);
    void closeFineSeek(bool play);
    // Index of the chapter playing at `timeMs` (0 without chapters).
    [[nodiscard]] std::size_t chapterAt(std::int64_t timeMs) const;
    // Plays streams resolved for m_watch's video.
    void beginPlayback(innertube::Player player);
    // Auto never exceeds the screen's resolution, in either orientation.
    [[nodiscard]] int screenLines() const;
    // Runs on a worker: loads the watch metadata of `videoId` into m_watch and returns the
    // related videos.
    [[nodiscard]] Result<innertube::Page> fetchWatch(const std::string& videoId);
    // Shows the watch metadata of `videoId` if it is still the video being watched.
    void applyWatch(const std::string& videoId, innertube::Watch watch);
    void beginSignIn();
    void onSignedIn();
    void reloadAccountFeeds();
    void refreshProfile();
    void switchAccount(std::size_t index);
    // "Library" chips of "You": the server's order, or the playlists by title.
    void sortLibrary(bool byTitle);
    // The signed-in account's own channel ("View channel").
    void openOwnChannel(const std::string& handle);
    void openChannel(std::string channelId, std::string name);
    void openSheet(Sheet sheet);
    void closeSheet();
    void showToast(std::string_view text);
    [[nodiscard]] double now() const;

    // Account actions, applied optimistically and reverted when the request fails.
    void rateVideo(innertube::Rating rating);
    void toggleSubscription(innertube::ChannelSummary& channel);
    void loadComments(const std::shared_ptr<CommentList>& list, std::string token);
    void openReplies(const innertube::Comment& parent);
    void rateComment(const std::shared_ptr<CommentList>& list, std::size_t index,
                     innertube::Rating rating);
    void submitComment();

    // Data.
    using PageRequest = std::function<Result<innertube::Page>()>;
    void load(const std::shared_ptr<Feed>& feed, PageRequest request);
    void loadMore(const std::shared_ptr<Feed>& feed);

    // Drawing.
    void drawTab(Frame& frame, ImVec2 min, ImVec2 max);
    void drawYou(Frame& frame, ImVec2 min, ImVec2 max);
    void drawSignInPrompt(Frame& frame, ImVec2 min, ImVec2 max);
    void drawSignIn(Frame& frame, ImVec2 min, ImVec2 max);
    void drawSearch(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max);
    void drawWatch(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max);
    void drawPlayer(Frame& frame, ImVec2 min, ImVec2 max);
    void drawPlayerMenu(Frame& frame, ImVec2 min, ImVec2 max);
    // Pressing and holding on [min, max] plays faster until released, with a pill at `pillTop`.
    // Call before anything else in the area handles taps: the release is not a tap. Returns
    // whether the speed-up is on.
    bool holdForSpeed(Frame& frame, ImVec2 min, ImVec2 max, float pillTop);
    // Dark pill centred on `centreX` below `top`, as the official app shows over the video.
    void drawPill(const Canvas& canvas, float centreX, float top, std::string_view label,
                  std::optional<Icon> icon = std::nullopt);
    // Draws the video in fullscreen, zoomed by pinching, with the zoom indicators over it.
    void drawZoomableVideo(Frame& frame, ImVec2 min, ImVec2 max);

    // Centred panel of rows over the player; returns the tapped row. Taps outside close it.
    struct MenuRow {
        std::string label;
        bool selected = false;
        bool submenu = false; // Opens another menu instead of being a choice.
    };

    [[nodiscard]] std::optional<std::size_t> drawMenu(Frame& frame, ImVec2 min, ImVec2 max,
                                                      std::string_view title,
                                                      std::span<const MenuRow> rows);
    void setFullscreen(bool fullscreen);
    void drawFeedScreen(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max);
    void drawTitleBar(Frame& frame, std::string_view title, ImVec2 min, float width);
    void drawHomeBar(Frame& frame, ImVec2 min, float width);
    // Launch animation over everything until the home feed is in (SplashScreen.cpp).
    void drawSplash(Frame& frame, ImVec2 display);
    void drawNavigationBar(Frame& frame, ImVec2 min, float width);
    void drawFeed(Frame& frame, std::shared_ptr<Feed> feed, ItemStyle style, ImVec2 min, ImVec2 max,
                  const ListHeader& header = {}, std::span<const ListInsert> inserts = {});
    void drawScreen(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max);
    // Everything but picture-in-picture: the screens, sheets, mini player and overlays.
    void drawInterface(Frame& frame, ImVec2 min, ImVec2 max, ImVec2 display);
    [[nodiscard]] float drawWatchHeader(Frame& frame, ImVec2 origin, float width);
    void drawChannelRow(Frame& frame, innertube::ChannelSummary& channel, ImVec2 origin,
                        float width);
    void drawSubscribeButton(Frame& frame, innertube::ChannelSummary& channel, ImVec2 min,
                             ImVec2 max);
    void drawSheet(Frame& frame, ImVec2 min, ImVec2 max);
    void drawCommentsSheet(Frame& frame, ImVec2 min, ImVec2 max);
    void drawDescriptionSheet(Frame& frame, ImVec2 min, ImVec2 max);
    void drawAccountsSheet(Frame& frame, ImVec2 min, ImVec2 max);
    void drawCommentList(Frame& frame, const std::shared_ptr<CommentList>& list, ImVec2 min,
                         ImVec2 max, bool allowReplies, const ListHeader& header = {});
    [[nodiscard]] float drawComment(Frame& frame, const std::shared_ptr<CommentList>& list,
                                    std::size_t index, ImVec2 origin, float width,
                                    bool allowReplies);
    bool drawCommentInput(Frame& frame, ImVec2 origin, float width, std::string_view hint);
    void drawChannel(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max);
    [[nodiscard]] float drawAccountHeader(Frame& frame, ImVec2 origin, float width);
    [[nodiscard]] float accountsSheetHeight(const Canvas& canvas) const;
    // Height of the modal sheet being shown, at most `available`.
    [[nodiscard]] float modalSheetHeight(const Canvas& canvas, float available) const;

    // Share and save (SavePage.cpp).
    void shareVideo(const innertube::Item& item, bool isShort = false);
    void openSave(const std::string& videoId);
    void toggleSaved(std::size_t index);
    void createPlaylist();
    [[nodiscard]] float saveSheetHeight(const Canvas& canvas, float available) const;
    void drawSaveSheet(Frame& frame, ImVec2 min, ImVec2 max);

    // Downloads and offline viewing (DownloadPage.cpp).
    void openDownload(const innertube::Item& item);
    void openDownloads();
    [[nodiscard]] float downloadSheetHeight(const Canvas& canvas) const;
    void drawDownloadSheet(Frame& frame, ImVec2 min, ImVec2 max);
    void drawDownloads(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max);
    // "You're offline" in place of a feed that failed for lack of a connection.
    void drawOffline(Frame& frame, const std::shared_ptr<Feed>& feed, ImVec2 min, ImVec2 max);
    // Short status of a download: size, progress or failure.
    [[nodiscard]] static std::string downloadStatus(const Download& download);
    void drawToast(Frame& frame, ImVec2 min, ImVec2 max);
    void drawShorts(Frame& frame, ImVec2 min, ImVec2 max);
    // The like animation of a double tap on a short: a white thumb popping in a burst.
    void drawLikeBurst(Frame& frame);
    void drawShort(Frame& frame, const innertube::Item& item, bool current, ImVec2 min, ImVec2 max);
    void playShort(const innertube::Item& item);
    // Fetches what the shorts next to `index` need so that swiping to them is instant.
    void prefetchShorts(std::span<const innertube::Item> items, std::size_t index);
    void openShort(std::size_t index);

    [[nodiscard]] Feed& shortsFeed() { return *m_tabFeeds[std::to_underlying(Tab::Shorts)]; }

    // Shorts shelves interleaved with the home feed, as in the official app.
    [[nodiscard]] std::vector<ListInsert> shortsShelves(const Canvas& c, float width);
    [[nodiscard]] float shortsShelfHeight(const Canvas& c, float width) const;
    // Grid of shorts [first, first + shelf size) shown inside the home feed.
    void drawShortsShelf(Frame& frame, ImVec2 origin, float width, std::size_t first);

    // Community posts (PostPage.cpp).
    // Starts loading the home feed's posts once the home and subscriptions feeds are in.
    void loadPosts();
    // Adds the home feed's post cards to `inserts`, keeping them ordered by position.
    void addPostInserts(const Canvas& c, float width, std::vector<ListInsert>& inserts);
    void openPost(const innertube::Post& post);
    void openPostReplies(const innertube::Comment& parent);
    void onPostAction(PostAction action, const innertube::Post& post);
    void drawPost(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max);

    Platform m_platform;
    Display m_display;
    http::Client m_http;
    auth::Session m_session;
    innertube::Api m_api;
    float m_density;
    Touch m_touch;
    Insets m_insets;
    EmojiSource m_emoji{
        .has = [this](char32_t c) { return m_platform.hasGlyph(c); },
        .draw = [this](char32_t c, float size) { return m_platform.drawGlyph(c, size); },
    };
    Fonts m_fonts;
    std::chrono::steady_clock::time_point m_start = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point m_lastFrame = m_start;

    Tab m_tab = Tab::Home;
    std::array<std::shared_ptr<Feed>, kTabCount> m_tabFeeds;
    std::shared_ptr<Feed> m_historyShelf = std::make_shared<Feed>();
    std::vector<Screen> m_stack;
    std::array<char, 256> m_query{};
    bool m_focusSearch = false;
    std::vector<std::string> m_searchHistory; // Newest first.
    std::vector<std::string> m_suggestions;
    std::string m_suggestedFor;   // Query the suggestions shown complete.
    std::string m_suggestPending; // Query being completed by a request in flight.
    innertube::SearchFilters m_filters;
    bool m_filtersOpen = false;
    float m_searchFiltersHeight = 0; // Measured on the previous frame.
    WatchState m_watch;
    SignInState m_signIn;
    SheetState m_sheet;
    SaveState m_save;
    DownloadSheetState m_downloadSheet;
    Toast m_toast;
    ShortsState m_shorts;
    std::shared_ptr<PostFeed> m_posts = std::make_shared<PostFeed>();
    MiniPlayerState m_mini;
    SplashState m_splash;
    std::array<char, 1024> m_draft{}; // Comment or reply being written.
    double m_tabShownAt = -1;

    std::unique_ptr<Thumbnails> m_thumbnails;
    std::unique_ptr<VectorIcons> m_icons;
    std::unique_ptr<Avatars> m_avatars;
    std::unique_ptr<media::VideoSurface> m_videoSurface;
    media::Player m_player;
    std::unique_ptr<Subtitles> m_subtitles;
    // Videos watched before the current one, left by "next" or related videos.
    std::vector<std::pair<innertube::Item, VideoSource>> m_watchHistory;
    bool m_fullscreen = false;
    bool m_speedHeld = false;     // Playing faster while a press is held on the video.
    float m_zoom = 1;             // Fullscreen zoom settled by the last pinch; 1 fits the video.
    double m_zoomedToFillAt = -1; // When a pinch settled on filling the screen, for the pill.
    bool m_pinchNearFill = false; // The live pinch is within the snap to fill, to tick on entry.
    bool m_usedFineSeek = false;  // Precise seeking was opened, so its hint is no longer shown.
    // Rotating to landscape enters fullscreen, until the user leaves it while still landscape.
    bool m_rotationEntersFullscreen = true;
    float m_speed = 1; // Playback speed chosen in the player menu, kept across videos.

    Settings m_settings;

    struct UpdateState {
        std::optional<Release> available;
        bool checking = false;
        bool offered = false; // The sheet was shown for `available` since launch.
        bool downloading = false;
        bool installing = false; // Handed to Android's installer.
        std::shared_ptr<std::atomic<std::int64_t>> received =
            std::make_shared<std::atomic<std::int64_t>>(0);
        std::string error;
    };

    UpdateState m_update;
    bool m_onScreen = true;                        // The window exists: the app is visible.
    bool m_typing = false;                         // The keyboard is up: the caret blinks.
    int m_subscriptionsFilter = 0;                 // Chip of Subscriptions (kSubscriptionFilters).
    Scroller m_channelsScroller{Axis::Horizontal}; // The channels row of Subscriptions.
    Scroller m_subscriptionChipsScroller{Axis::Horizontal};
    float m_profileHeight = 0;     // Of the "You" header, measured on the previous frame.
    bool m_libraryByTitle = false; // "Library" sorted by title.
    innertube::Item m_actionItem;  // Of the ⋮ menu.
    std::string m_ownChannelId;    // The signed-in account's channel, once resolved.
    std::unordered_set<std::string> m_seenNotifications; // Uploads opened behind the bell.
    bool m_notificationsBaseline = false;                // What counted as seen was recorded once.
    std::unordered_set<std::string> m_unreadAtOpen;      // New when the inbox was opened.
    bool m_pictureInPicture = false;                     // Shown in a picture-in-picture window.
    std::optional<Platform::Playback> m_published;       // Last state given to the media session.
    double m_publishedAt = 0;
    bool m_pipAllowed = false;
    std::pair<int, int> m_pipSize{};
    TaskQueue m_tasks; // Workers are joined before anything they reference is destroyed.
    // After the task queue it posts progress to, so its thread stops first.
    std::unique_ptr<Downloads> m_downloads;
};

} // namespace sloptube::app
