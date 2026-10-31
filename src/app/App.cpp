#include "app/App.hpp"

#include "app/DataFiles.hpp"
#include "app/ui/Motion.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"
#include "app/util/Files.hpp"
#include "core/innertube/Storyboard.hpp"

#include <GLES3/gl3.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/looper.h>
#include <android_native_app_glue.h>
#include <imgui.h>
#include <imgui_impl_android.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <ranges>
#include <thread>
#include <tuple>

namespace sloptube::app {
namespace {

using innertube::Item;
using innertube::ItemKind;

constexpr std::size_t kWorkerThreads = 4;
constexpr std::size_t kThumbnailCapacity = 48;
constexpr int kThumbnailDecodeWidth = 480; // Source resolution of hqdefault.jpg.
constexpr std::string_view kWordmarkAccent = "Slop";
constexpr std::string_view kWordmarkRest = "Tube";

// Account feeds behind the tabs; Shorts and You are loaded separately.
constexpr std::array<std::pair<Tab, innertube::Feed>, 3> kTabFeedSources{{
    {Tab::Home, innertube::Feed::Home},
    {Tab::Subscriptions, innertube::Feed::Subscriptions},
    {Tab::You, innertube::Feed::Playlists},
}};

constexpr float kSectionSpacing = 24; // dp
// Seek preview, measured on the official app: the time in a 31 dp pill whose bottom is 36 dp
// above the bar, over the dimmed storyboard frame.
constexpr float kTimePillHeight = 31;  // dp
constexpr float kTimePillPadding = 16; // dp on each side of the time.
constexpr float kTimePillGap = 36;     // dp between the pill and the bar.
constexpr float kTimePillSize = 16;    // sp
constexpr ImU32 kTimePill = IM_COL32(0x00, 0x00, 0x00, 0x99);
constexpr ImU32 kScrubDim = IM_COL32(0x00, 0x00, 0x00, 0x4d);
constexpr float kChapterTitleGap = 8; // dp between the chapter title and the time pill.
// The bar is split at chapters by gaps, wider while seeking, as in the official app.
constexpr float kChapterGap = 2;        // dp
constexpr float kSeekingChapterGap = 3; // dp
constexpr std::string_view kChapterSeparator = " • ";
// Speeds of the player menu, as the official app offers them.
constexpr std::array<float, 8> kSpeeds{0.25f, 0.5f, 0.75f, 1, 1.25f, 1.5f, 1.75f, 2};

// "Normal" for 1, else "1.5x" (the official app's wording).
std::string speedLabel(float speed) {
    if (speed == 1) {
        return std::string(tr(Text::NormalSpeed));
    }
    std::string label = std::format("{:g}x", speed);
    return label;
}

// A broadcast's time row: a red dot before "LIVE".
constexpr float kLiveDot = 8;    // dp
constexpr float kLiveDotGap = 6; // dp
// Precise seeking, as in the official app: pulling 45 dp up from where a seek along
// the bar began opens a film strip of square 72 dp frames under a 3 dp line, pulling as far down
// closes it. Above the strip: the time, then a 56 dp play button.
constexpr float kFinePull = 45;             // dp
constexpr float kFilmstripFrame = 72;       // dp
constexpr float kFilmstripLine = 3;         // dp
constexpr float kFilmstripMargin = 10;      // dp under the strip.
constexpr float kFineSpacing = 12;          // dp between the strip, the time and the play button.
constexpr float kFineTimePaddingX = 12;     // dp
constexpr float kFineTimePaddingY = 6;      // dp
constexpr float kFinePlayButton = 56;       // dp
constexpr std::int64_t kFineFrameMs = 2000; // A frame's span when the video has no storyboard.

constexpr float kSkipOffset = 96;            // dp from play/pause to the previous and next buttons.
constexpr float kTopButtonSpacing = 48;      // dp between buttons along the player's top edge.
constexpr float kSubtitlesOverControls = 44; // dp from the player's bottom, clear of the time.
constexpr float kSubtitlesMargin = 12;       // dp from the player's bottom.
constexpr ImU32 kDisabledControl = IM_COL32(0xff, 0xff, 0xff, 0x4d);
constexpr float kHoldSeconds = 0.4f; // Press duration that starts the speed-up.
constexpr float kHeldSpeed = 2;
constexpr float kSpeedPillTop = 16; // dp from the top of the video.
// Fullscreen pinch, as in the official app: a pinch settling near the zoom that fills the screen
// snaps to it, one settling nearer the fitted size goes back to it, any other zoom stays.
constexpr float kMaxZoom = 8;            // Times the fitted size.
constexpr float kFillSnap = 1.15f;       // Zooms up to this much past filling snap to filling.
constexpr float kZoomPillTop = 40;       // dp from the top of the screen.
constexpr double kZoomPillSeconds = 2;   // The "zoomed to fill" pill stays this long.
constexpr float kZoomBadgeSize = 40;     // dp, circled zoom factor shown while pinching.
constexpr float kZoomBadgeStroke = 1.5f; // dp

// "01:59", minutes always on two digits as in the official app's seek pills.
std::string pillTime(std::int64_t timeMs) {
    const std::int64_t seconds = timeMs / 1000;
    return seconds >= 3600
               ? std::format("{}:{:02}:{:02}", seconds / 3600, seconds / 60 % 60, seconds % 60)
               : std::format("{:02}:{:02}", seconds / 60, seconds % 60);
}

// The progress bar over [x0, x1], filled up to `filledX`, split at the chapters.
void drawProgressBar(const Canvas& c, float x0, float x1, float y0, float y1, float filledX,
                     std::span<const innertube::Chapter> chapters, std::int64_t durationMs,
                     float gapDp) {
    const auto xAt = [&](std::int64_t ms) {
        return x0 + (x1 - x0) * std::clamp(static_cast<float>(ms) / static_cast<float>(durationMs),
                                           0.0f, 1.0f);
    };
    const std::size_t segments = std::max<std::size_t>(chapters.size(), 1);
    const float halfGap = c.dp(gapDp) * 0.5f;
    for (std::size_t i = 0; i < segments; ++i) {
        const float from = i == 0 ? x0 : xAt(chapters[i].startMs) + halfGap;
        const float to = i + 1 >= segments ? x1 : xAt(chapters[i + 1].startMs) - halfGap;
        if (to <= from) {
            continue;
        }
        c.rect({from, y0}, {to, y1}, theme::kTrack);
        if (filledX > from) {
            c.rect({from, y0}, {std::min(to, filledX), y1}, theme::kAccent);
        }
    }
}

constexpr float kScreenSlide = 0.2f; // Fraction of the width a pushed screen slides in from.
constexpr float kWatchRise = 0.12f;  // Fraction of the height the watch page rises from.
// The top bar, as measured on the official app.
constexpr float kBarIconInset = 24;        // dp from the right edge to the last icon's centre.
constexpr float kBarIconSpacing = 48;      // dp between icon centres.
constexpr float kAccountsPillPadding = 14; // dp inside "Accounts".
constexpr float kAccountsPillExtra = 50;   // dp of "Accounts" besides its label (chevron).
constexpr float kChipRadius = 8;           // dp

} // namespace

App::App(android_app* native)
    : m_platform(native)
    , m_http({.caPath = std::string(Platform::trustedCertificates())})
    , m_session(m_http, m_platform.dataDirectory() / kSessionFile)
    , m_api(m_http, m_session, m_platform.locale())
    , m_density(m_platform.density())
    , m_touch(theme::kTouchSlop * m_density)
    , m_player(m_http, [looper = native->looper] { ALooper_wake(looper); })
    , m_tasks(kWorkerThreads, [looper = native->looper] { ALooper_wake(looper); }) {
    setLanguage(languageFor(m_platform.locale().language));
    for (auto& feed : m_tabFeeds) {
        feed = std::make_shared<Feed>();
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    // A frame interrupted by an exception is recovered (abandonFrame), never stopped on.
    io.ConfigErrorRecovery = true;
    io.ConfigErrorRecoveryEnableAssert = false;
    io.ConfigErrorRecoveryEnableTooltip = false;

    // The interface size (Settings, and Android's text size) sets the density everything uses.
    m_settings = Settings::load(m_platform.dataDirectory() / kSettingsFile);
    applyScale();
    // Glyphs are rasterised on demand at any size, so the fonts follow later scale changes.
    m_fonts = loadFonts(*io.Fonts, theme::kTitleSize * m_density, m_platform.locale().language,
                        m_platform.locale().region, m_emoji);

    m_thumbnails =
        std::make_unique<Thumbnails>(m_http, m_tasks, kThumbnailDecodeWidth, kThumbnailCapacity);
    m_avatars =
        std::make_unique<Avatars>(m_api, m_tasks, m_platform.dataDirectory() / kAvatarCacheFile);
    m_videoSurface = std::make_unique<media::VideoSurface>(m_display.eglDisplay());
    m_subtitles = std::make_unique<Subtitles>(m_http, m_api, m_tasks,
                                              m_platform.dataDirectory() / kSubtitlesFile,
                                              m_platform.locale().language);
    m_downloads = std::make_unique<Downloads>(m_http, m_api, m_tasks,
                                              m_platform.dataDirectory() / kDownloadsDirectory);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    applySettings();
    loadSeenNotifications();
    // An update downloaded earlier is installed by now, or no longer wanted.
    std::error_code removeError;
    std::filesystem::remove(m_platform.dataDirectory() / kUpdateFile, removeError);
    if (m_settings.checkUpdates) {
        checkForUpdate(false);
    }
    if (auto history = readText(m_platform.dataDirectory() / kSearchHistoryFile)) {
        for (const auto line : std::views::split(*history, '\n')) {
            if (!line.empty()) {
                m_searchHistory.emplace_back(line.begin(), line.end());
            }
        }
    }
    // Java events (media buttons, picture-in-picture, notification taps) arrive on Java threads.
    setBridgeHandler([this](BridgeEvent event) {
        m_tasks.post(
            [this, event = std::move(event)]() mutable { onBridgeEvent(std::move(event)); });
    });

    reloadAccountFeeds();
    refreshProfile();
}

App::~App() {
    setBridgeHandler(nullptr);
    m_platform.stopPlayback();
    m_signIn.cancelled->store(true);
    m_videoSurface->release();
    m_player.close();
    ImGui_ImplOpenGL3_Shutdown();
    if (m_display.hasSurface()) {
        ImGui_ImplAndroid_Shutdown();
    }
    ImGui::DestroyContext();
}

void App::applyScale() {
    // Android's own text size counts too: whoever made the phone's text bigger wants this. Above
    // the cap, a phone screen no longer holds the layouts.
    constexpr float kMaxScale = 1.45f;
    const float scale = std::clamp(kInterfaceScales[static_cast<std::size_t>(std::clamp(
                                       m_settings.interfaceSize, 0, kInterfaceSizes - 1))] *
                                       std::max(1.0f, m_platform.fontScale()),
                                   1.0f, kMaxScale);
    m_density = m_platform.density() * scale;
    m_touch.setSlop(theme::kTouchSlop * m_density);

    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark(&style);
    style.ScaleAllSizes(m_density);
    style.FontSizeBase = theme::kTitleSize * m_density;
    style.FrameRounding = theme::kPillRadius * m_density;
    style.FramePadding = {theme::kGutter * m_density, theme::kGutter * 0.5f * m_density};
    style.Colors[ImGuiCol_FrameBg] = ImGui::ColorConvertU32ToFloat4(theme::kSurface);
    style.Colors[ImGuiCol_FrameBgHovered] = style.Colors[ImGuiCol_FrameBg];
    style.Colors[ImGuiCol_FrameBgActive] = style.Colors[ImGuiCol_FrameBg];
    style.Colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(theme::kTextPrimary);
    style.Colors[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(theme::kTextSecondary);
    style.Colors[ImGuiCol_NavCursor] = {0, 0, 0, 0};
    // The navigation icons are rasterised at their exact pixel size.
    m_icons =
        std::make_unique<VectorIcons>(static_cast<int>(std::lround(theme::kIconSize * m_density)));
}

void App::onWindowCreated(ANativeWindow* window) {
    m_onScreen = true;
    m_player.setVideoHidden(false);
    m_display.attach(window);
    ImGui_ImplAndroid_Init(window);
    onInsetsChanged();
}

void App::onWindowDestroyed() {
    // Playback goes on in the background (Settings::backgroundPlay, onStop), unseen.
    m_onScreen = false;
    m_player.setVideoHidden(true);
    ImGui_ImplAndroid_Shutdown();
    m_display.detach();
}

void App::onInsetsChanged() {
    m_insets = m_platform.systemBarInsets();
}

void App::onPause() {
    // Playback goes on into picture-in-picture, or stops when the app is gone (onStop).
}

std::int32_t App::onInput(const AInputEvent* event) {
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        m_touch.onMotionEvent(event);
        ImGui_ImplAndroid_HandleInputEvent(event);
        return 1;
    }
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_KEY) {
        return 0;
    }
    // The device's own buttons are the system's: volume, media keys, camera...
    switch (AKeyEvent_getKeyCode(event)) {
    case AKEYCODE_VOLUME_UP:
    case AKEYCODE_VOLUME_DOWN:
    case AKEYCODE_VOLUME_MUTE:
    case AKEYCODE_MUTE:
    case AKEYCODE_MEDIA_PLAY_PAUSE:
    case AKEYCODE_MEDIA_PLAY:
    case AKEYCODE_MEDIA_PAUSE:
    case AKEYCODE_MEDIA_NEXT:
    case AKEYCODE_MEDIA_PREVIOUS:
    case AKEYCODE_MEDIA_STOP:
    case AKEYCODE_HEADSETHOOK:
    case AKEYCODE_CAMERA:
    case AKEYCODE_POWER:
        return 0;
    default:
        break;
    }
    if (AKeyEvent_getKeyCode(event) == AKEYCODE_BACK) {
        if (!canGoBack()) {
            return 0; // Let the system close the activity.
        }
        if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_UP) {
            goBack();
        }
        return 1;
    }
    ImGui_ImplAndroid_HandleInputEvent(event);
    if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN) {
        if (const char32_t c = m_platform.unicodeChar(event); c >= U' ') {
            ImGui::GetIO().AddInputCharacter(static_cast<unsigned>(c));
        }
    }
    return 1;
}

// --- Navigation ---------------------------------------------------------------------------------

bool App::canGoBack() const {
    return m_fullscreen || m_watch.menu != PlayerMenu::None || m_sheet.kind != Sheet::None ||
           !m_stack.empty() || m_tab != Tab::Home || m_mini.watch.has_value();
}

void App::goBack() {
    if (m_watch.menu != PlayerMenu::None) {
        switch (m_watch.menu) {
        case PlayerMenu::Translations:
            m_watch.menu = PlayerMenu::Subtitles;
            break;
        case PlayerMenu::Quality:
        case PlayerMenu::Speed:
            m_watch.menu = PlayerMenu::Settings;
            break;
        default:
            m_watch.menu = PlayerMenu::None;
            break;
        }
        m_watch.menuScroller.reset();
        return;
    }
    if (m_fullscreen) {
        setFullscreen(false);
        return;
    }
    if (m_sheet.kind != Sheet::None) {
        if (m_watch.replies && m_sheet.kind == Sheet::Comments) {
            m_watch.replies.reset();
            m_watch.replyParent.reset();
        } else if (m_save.creating && m_sheet.kind == Sheet::Save) {
            m_save.creating = false;
            ImGui::ClearActiveID();
        } else {
            closeSheet();
        }
        return;
    }
    if (m_stack.empty()) {
        if (m_tab == Tab::Home && m_mini.watch) {
            closeMiniPlayer();
            return;
        }
        selectTab(Tab::Home);
        return;
    }
    if (const auto& post = m_stack.back().post; post && post->replies) {
        post->replies.reset();
        return;
    }
    switch (m_stack.back().route) {
    case Route::Watch: {
        // Like the official app, leaving the watch page keeps the video in the mini player.
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        minimizeVideo({0, m_insets.top},
                      {display.x, m_insets.top + display.x * theme::kAspect16x9});
        return;
    }
    case Route::SignIn:
        m_signIn.cancelled->store(true);
        break;
    default:
        break;
    }
    m_stack.pop_back();
}

void App::selectTab(Tab tab) {
    while (!m_stack.empty()) {
        goBack();
    }
    if (tab == m_tab) {
        m_tabFeeds[std::to_underlying(tab)]->scroller.reset(); // Re-tap scrolls to top.
    } else {
        m_tabShownAt = now();
        if (m_tab == Tab::Shorts) {
            closeVideo();
            m_player.closeStandby();
            m_shorts.playing.clear();
            m_shorts.standby.clear();
        }
    }
    m_tab = tab;
}

void App::push(Screen screen) {
    screen.openedAt = now();
    m_sheet = SheetState{};
    m_stack.push_back(std::move(screen));
}

void App::later(std::function<void()> action) {
    m_tasks.post(std::move(action));
}

void App::openItem(Item item, VideoSource source) {
    switch (item.kind) {
    case ItemKind::Video:
    case ItemKind::Short:
    case ItemKind::Live:
        openVideo(item, std::move(source));
        break;
    case ItemKind::Playlist: {
        if (innertube::isMix(item.id)) {
            openMix(item.id);
            break;
        }
        Screen screen{.route = Route::Playlist, .argument = item.id, .title = item.title};
        load(screen.feed, [this, id = item.id] { return m_api.playlist(id); });
        push(std::move(screen));
        break;
    }
    case ItemKind::Channel:
        openChannel(item.id, item.title);
        break;
    }
}

void App::openVideo(Item item, VideoSource source, bool recordHistory) {
    closeMiniPlayer();
    if (!m_stack.empty() && m_stack.back().route == Route::Watch) {
        if (recordHistory) {
            m_watchHistory.emplace_back(m_watch.item, m_watch.source);
        }
        closeVideo();
        m_stack.pop_back();
    }
    const std::string id = item.id;
    m_watch = WatchState{.item = std::move(item), .source = std::move(source)};
    if (auto local = m_downloads->localCaptions(id)) {
        m_subtitles->openLocal(id, std::move(*local));
    } else {
        m_subtitles->open(id);
    }

    Screen screen{.route = Route::Watch, .argument = id};
    // One `next` request yields both the page details and the related videos.
    load(screen.feed, [this, id] { return fetchWatch(id); });
    push(std::move(screen));
    m_player.closeStandby(); // Only Shorts preload.
    m_shorts.standby.clear();
    startPlayback(id);
}

void App::openMix(const std::string& playlistId) {
    // Its first video plays with the mix as the list it came from, so "next" and autoplay go on
    // through the mix.
    m_tasks.run([this, playlistId] {
        auto queue = m_api.mix(playlistId);
        m_tasks.post([this, queue = std::move(queue)]() mutable {
            if (!queue) {
                showToast(friendlyError(queue.error().message));
                return;
            }
            auto feed = std::make_shared<Feed>();
            feed->loaded = true;
            feed->page = std::move(*queue);
            openVideo(feed->page.items.front(), VideoSource{.feed = feed, .index = 0});
        });
    });
}

Result<innertube::Page> App::fetchWatch(const std::string& videoId) {
    auto watch = m_api.watch(videoId);
    if (!watch) {
        return std::unexpected(watch.error());
    }
    auto related = std::move(watch->related);
    m_tasks.post([this, videoId, watch = std::move(*watch)]() mutable {
        applyWatch(videoId, std::move(watch));
    });
    return related;
}

void App::applyWatch(const std::string& videoId, innertube::Watch watch) {
    if (m_watch.item.id != videoId) {
        return;
    }
    m_avatars->remember(watch.channel.id, watch.channel.avatar);
    m_avatars->remember(videoId, watch.channel.avatar);
    m_watch.comments = std::make_shared<CommentList>();
    loadComments(m_watch.comments, watch.commentsToken);
    m_watch.info = std::move(watch);
}

void App::loadStoryboard(const std::string& videoId) {
    m_tasks.run([this, videoId] {
        auto storyboard = m_api.storyboard(videoId);
        if (!storyboard) {
            return; // Scrubbing then shows the time alone.
        }
        m_tasks.post([this, videoId, storyboard = std::move(*storyboard)]() mutable {
            if (m_watch.item.id == videoId) {
                m_watch.storyboard = std::move(storyboard);
            }
        });
    });
}

std::size_t App::chapterAt(std::int64_t timeMs) const {
    if (!m_watch.info) {
        return 0;
    }
    const auto& chapters = m_watch.info->chapters;
    const auto after = std::ranges::upper_bound(chapters, timeMs, {}, &innertube::Chapter::startMs);
    return after == chapters.begin() ? 0 : static_cast<std::size_t>(after - chapters.begin() - 1);
}

void App::drawScrubPreview(Frame& frame, ImVec2 min, ImVec2 max, std::int64_t timeMs, float barY,
                           std::string_view label) {
    const Canvas& c = frame.canvas;
    // The largest frames: they are stretched over the whole player.
    auto preview = innertube::storyboardFrame(m_watch.storyboard, std::numeric_limits<int>::max(),
                                              timeMs, m_player.durationUs() / 1000);
    const Texture* sheet = preview ? m_thumbnails->get(preview->url, preview->sheetWidth) : nullptr;
    if (sheet) {
        m_watch.shownPreview = preview;
    } else if (m_watch.shownPreview) {
        preview = m_watch.shownPreview;
        sheet = m_thumbnails->get(preview->url, preview->sheetWidth);
        frame.animating = true; // Until the wanted sheet arrives.
    }
    if (preview && sheet) {
        const float aspect =
            static_cast<float>(preview->width) / static_cast<float>(preview->height);
        float width = max.x - min.x;
        float height = width / aspect;
        if (height > max.y - min.y) {
            height = max.y - min.y;
            width = height * aspect;
        }
        const ImVec2 at{(min.x + max.x - width) * 0.5f, (min.y + max.y - height) * 0.5f};
        const auto u = [&](int x) {
            return static_cast<float>(x) / static_cast<float>(sheet->width);
        };
        const auto v = [&](int y) {
            return static_cast<float>(y) / static_cast<float>(sheet->height);
        };
        c.rect(min, max, IM_COL32_BLACK);
        c.list()->AddImage(sheet->ref(), at, {at.x + width, at.y + height},
                           {u(preview->x), v(preview->y)},
                           {u(preview->x + preview->width), v(preview->y + preview->height)});
    }
    c.rect(min, max, kScrubDim);

    const std::string time = label.empty() ? pillTime(timeMs) : std::string(label);
    const Canvas medium = c.with(Weight::Medium);
    const ImVec2 extent = medium.measure(kTimePillSize, time);
    const float width = extent.x + c.dp(kTimePillPadding) * 2;
    const ImVec2 pillMin{(min.x + max.x - width) * 0.5f,
                         barY - c.dp(kTimePillGap) - c.dp(kTimePillHeight)};
    const ImVec2 pillMax{pillMin.x + width, pillMin.y + c.dp(kTimePillHeight)};
    c.list()->AddRectFilled(pillMin, pillMax, kTimePill, c.dp(kTimePillHeight) * 0.5f);
    medium.text(
        {pillMin.x + c.dp(kTimePillPadding), pillMin.y + (c.dp(kTimePillHeight) - extent.y) * 0.5f},
        kTimePillSize, theme::kTextPrimary, time);
    if (m_watch.info && !m_watch.info->chapters.empty()) {
        const float gutter = c.dp(theme::kGutter);
        medium.line(
            {min.x + gutter, pillMin.y - c.dp(kChapterTitleGap) - c.lineHeight(theme::kBodySize)},
            max.x - min.x - gutter * 2, theme::kBodySize, theme::kTextPrimary,
            m_watch.info->chapters[chapterAt(timeMs)].title, TextAlign::Center);
    }
}

void App::drawFineSeek(Frame& frame, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    ImDrawList* list = c.list();
    const std::int64_t durationMs = m_player.durationUs() / 1000;
    const float size = c.dp(kFilmstripFrame);
    const int sizePx = static_cast<int>(size);
    std::int64_t stepMs = innertube::storyboardInterval(m_watch.storyboard, sizePx, durationMs);
    if (stepMs <= 0) {
        stepMs = kFineFrameMs;
    }
    c.rect(min, max, theme::kScrim);

    // Frame k shows [k, k + 1) steps; the strip moves so the line marks m_watch.fineMs.
    const float centreX = (min.x + max.x) * 0.5f;
    const float top = max.y - c.dp(kFilmstripMargin) - size;
    const float offset = static_cast<float>(m_watch.fineMs) / static_cast<float>(stepMs);
    const auto first = static_cast<std::int64_t>(std::floor(offset - (centreX - min.x) / size));
    const auto last = static_cast<std::int64_t>(std::ceil(offset + (max.x - centreX) / size));
    list->PushClipRect({min.x, top}, {max.x, top + size}, true);
    for (std::int64_t k = std::max<std::int64_t>(first, 0); k <= last && k * stepMs < durationMs;
         ++k) {
        const float x = centreX + (static_cast<float>(k) - offset) * size;
        const ImVec2 at{x, top};
        const ImVec2 to{x + size, top + size};
        const auto preview =
            innertube::storyboardFrame(m_watch.storyboard, sizePx, k * stepMs, durationMs);
        const Texture* sheet =
            preview ? m_thumbnails->get(preview->url, preview->sheetWidth) : nullptr;
        if (!sheet) {
            c.rect(at, to, theme::kPlaceholder);
            frame.animating |= preview.has_value(); // Until the sheet arrives.
            continue;
        }
        // Square frames: the middle of each 16:9 cell.
        const int crop = std::min(preview->width, preview->height);
        const auto u = [&](int px) {
            return static_cast<float>(px) / static_cast<float>(sheet->width);
        };
        const auto v = [&](int px) {
            return static_cast<float>(px) / static_cast<float>(sheet->height);
        };
        const int left = preview->x + (preview->width - crop) / 2;
        const int upper = preview->y + (preview->height - crop) / 2;
        list->AddImage(sheet->ref(), at, to, {u(left), v(upper)},
                       {u(left + crop), v(upper + crop)});
    }
    list->PopClipRect();
    const float line = c.dp(kFilmstripLine);
    c.rect({centreX - line * 0.5f, top}, {centreX + line * 0.5f, top + size}, IM_COL32_WHITE);

    // The time, then play, centred above the strip; close in the corner.
    const std::string time = pillTime(m_watch.fineMs);
    const ImVec2 extent = c.measure(kTimePillSize, time);
    const float timeHeight = c.lineHeight(kTimePillSize) + c.dp(kFineTimePaddingY) * 2;
    const ImVec2 timeMin{centreX - extent.x * 0.5f - c.dp(kFineTimePaddingX),
                         top - c.dp(kFineSpacing) - timeHeight};
    const ImVec2 timeMax{centreX + extent.x * 0.5f + c.dp(kFineTimePaddingX),
                         timeMin.y + timeHeight};
    c.rect(timeMin, timeMax, theme::kOverlayButton, c.dp(theme::kPillRadius));
    c.text({timeMin.x + c.dp(kFineTimePaddingX), timeMin.y + c.dp(kFineTimePaddingY)},
           kTimePillSize, theme::kTextPrimary, time);
    const float radius = c.dp(kFinePlayButton) * 0.5f;
    const ImVec2 play{centreX, timeMin.y - c.dp(kFineSpacing) - radius};
    list->AddCircleFilled(play, radius, theme::kOverlayButton);
    c.icon(Icon::Play, play, theme::kIconSize, theme::kTextPrimary);
    if (frame.touch.tapped({play.x - radius, play.y - radius},
                           {play.x + radius, play.y + radius})) {
        closeFineSeek(true);
    } else if (iconButton(frame, Icon::Close,
                          {max.x - c.dp(theme::kTopBarHeight) * 0.5f,
                           min.y + c.dp(theme::kTopBarHeight) * 0.5f},
                          theme::kTextPrimary)) {
        closeFineSeek(m_watch.fineResume);
    }
}

void App::closeFineSeek(bool play) {
    m_watch.fine = false;
    m_player.setPaused(!play);
    m_watch.controlsShownAt = now();
}

void App::startPlayback(const std::string& videoId) {
    loadStoryboard(videoId);
    // Downloaded videos play from disk, with or without a connection.
    if (auto local = m_downloads->localPlayer(videoId)) {
        m_watch.details = local->details;
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        m_player.open(std::move(*local), static_cast<int>(std::min(display.x, display.y)));
        return;
    }
    m_tasks.run([this, id = videoId] {
        auto player = m_api.player(id);
        m_tasks.post([this, id, player = std::move(player)] {
            if (m_watch.item.id != id) {
                return; // The user left before the stream resolved.
            }
            if (!player) {
                m_watch.error = player.error().message;
                return;
            }
            beginPlayback(std::move(*player));
        });
    });
}

void App::beginPlayback(innertube::Player player) {
    m_watch.details = player.details;
    m_player.open(std::move(player), screenLines());
    if (m_player.qualities().empty()) {
        m_watch.error = tr(Text::PlaybackFailed);
    }
}

int App::screenLines() const {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    return static_cast<int>(std::min(display.x, display.y));
}

void App::playPrevious() {
    if (m_watchHistory.empty()) {
        return;
    }
    auto [item, source] = std::move(m_watchHistory.back());
    m_watchHistory.pop_back();
    openVideo(std::move(item), std::move(source), false);
}

std::optional<std::pair<Item, VideoSource>> App::nextVideo() const {
    const auto firstPlayable =
        [&](const std::shared_ptr<Feed>& feed,
            std::size_t from) -> std::optional<std::pair<Item, VideoSource>> {
        const auto& items = feed->page.items;
        for (std::size_t index = from; index < items.size(); ++index) {
            const Item& item = items[index];
            if ((item.kind == ItemKind::Video || item.kind == ItemKind::Live) &&
                item.id != m_watch.item.id) {
                return std::pair{item, VideoSource{.feed = feed, .index = index}};
            }
        }
        return std::nullopt;
    };
    if (const auto& feed = m_watch.source.feed) {
        // The list may have been reloaded since; find the video again by id.
        const auto& items = feed->page.items;
        std::size_t index = m_watch.source.index;
        if (index >= items.size() || items[index].id != m_watch.item.id) {
            const auto found = std::ranges::find(items, m_watch.item.id, &Item::id);
            index = static_cast<std::size_t>(found - items.begin());
        }
        if (index < items.size()) {
            if (auto next = firstPlayable(feed, index + 1)) {
                return next;
            }
        }
    }
    if (!m_stack.empty() && m_stack.back().route == Route::Watch) {
        return firstPlayable(m_stack.back().feed, 0); // Related videos.
    }
    return std::nullopt;
}

void App::closeVideo() {
    m_mini.watch.reset();
    m_subtitles->close();
    m_speedHeld = false;
    m_player.setSpeed(m_speed);
    m_watch.item.id.clear(); // Drops stream and metadata results still in flight.
    m_videoSurface->release();
    m_player.close();
}

void App::beginSignIn() {
    m_signIn.cancelled->store(true);
    m_signIn = SignInState{};
    push(Screen{.route = Route::SignIn});

    m_tasks.run([this, cancelled = m_signIn.cancelled] {
        auto code = m_session.beginSignIn();
        if (!code) {
            m_tasks.post([this, cancelled] {
                if (!cancelled->load()) {
                    m_signIn.failed = true;
                }
            });
            return;
        }
        m_tasks.post([this, cancelled, code = *code] {
            if (!cancelled->load()) {
                m_signIn.code = code;
            }
        });

        auto interval = code->pollInterval;
        while (!cancelled->load()) {
            std::this_thread::sleep_for(interval);
            const auto state = m_session.pollSignIn(*code);
            if (!state || *state == auth::PollState::Pending) {
                continue;
            }
            if (*state == auth::PollState::SlowDown) {
                interval *= 2;
                continue;
            }
            const bool authorized = *state == auth::PollState::Authorized;
            m_tasks.post([this, cancelled, authorized] {
                if (cancelled->load()) {
                    return;
                }
                if (authorized) {
                    onSignedIn();
                } else {
                    m_signIn.failed = true;
                }
            });
            return;
        }
    });
}

void App::onSignedIn() {
    m_signIn.cancelled->store(true);
    if (!m_stack.empty() && m_stack.back().route == Route::SignIn) {
        m_stack.pop_back();
    }
    reloadAccountFeeds();
    refreshProfile();
    selectTab(Tab::You);
}

void App::reloadAccountFeeds() {
    for (auto& feed : m_tabFeeds) {
        feed = std::make_shared<Feed>();
    }
    m_historyShelf = std::make_shared<Feed>();
    m_historyShelf->scroller = Scroller(Axis::Horizontal);
    m_shorts = ShortsState{};
    m_posts = std::make_shared<PostFeed>();
    const bool signedIn = m_session.signedIn();
    // Home first: the launch animation waits for it.
    if (signedIn) {
        for (const auto& [tab, source] : kTabFeedSources) {
            load(m_tabFeeds[std::to_underlying(tab)],
                 [this, source] { return m_api.feed(source); });
        }
    }
    load(m_tabFeeds[std::to_underlying(Tab::Shorts)], [this] { return m_api.shorts(); });
    if (!signedIn) {
        return;
    }
    load(m_historyShelf, [this] { return m_api.feed(innertube::Feed::History); });
}

// --- Data ---------------------------------------------------------------------------------------

void App::load(const std::shared_ptr<Feed>& feed, PageRequest request) {
    if (!feed->request) {
        feed->request = request; // Only the first page's request is replayed on retry.
    }
    feed->loading = true;
    feed->error.clear();
    const bool append = feed->loaded && !feed->page.items.empty();
    m_tasks.run([this, target = std::weak_ptr(feed), request = std::move(request), append] {
        auto result = request();
        m_tasks.post([target, append, result = std::move(result)]() mutable {
            const auto feed = target.lock();
            if (!feed) {
                return;
            }
            feed->loading = false;
            feed->loaded = true;
            if (!result) {
                feed->error = std::move(result.error().message);
                return;
            }
            if (feed->keep) {
                std::erase_if(result->items, [&](const auto& item) { return !feed->keep(item); });
            }
            if (append) {
                innertube::appendPage(feed->page, std::move(*result));
            } else {
                feed->page = std::move(*result);
            }
        });
    });
}

void App::loadMore(const std::shared_ptr<Feed>& feed) {
    innertube::Page cursor{.continuation = feed->page.continuation, .source = feed->page.source};
    load(feed, [this, cursor = std::move(cursor)] { return m_api.continuation(cursor); });
}

// --- Frame --------------------------------------------------------------------------------------

bool App::frame() {
    m_tasks.drainMainThread();
    updateAutoplay();
    syncPlayback();
    // A newer release found at launch is offered once the launch animation is over, on a free
    // screen.
    if (m_update.available && !m_update.offered && m_splash.finished &&
        m_sheet.kind == Sheet::None && m_onScreen && !m_pictureInPicture) {
        m_update.offered = true;
        openSheet(Sheet::Update);
    }
    if (m_watch.details && m_watch.details->live && watching() && m_onScreen) {
        pollLiveChat();
    }
    if (!m_display.hasSurface()) {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    const float seconds = std::chrono::duration<float>(now - m_lastFrame).count();
    m_lastFrame = now;
    m_touch.beginFrame();
    m_thumbnails->beginFrame();
    m_videoSurface->update(m_player.imageReader());

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(display);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);

    Frame frame{
        .canvas = Canvas(ImGui::GetWindowDrawList(), m_fonts, m_density),
        .touch = m_touch,
        .thumbnails = *m_thumbnails,
        .avatars = *m_avatars,
        .now = now,
        .time = std::chrono::duration<double>(now - m_start).count(),
        .seconds = seconds,
    };
    const ImVec2 min{0, m_insets.top};
    const ImVec2 max{display.x, display.y - m_insets.bottom};
    frame.animating |= m_thumbnails->uploading(); // Images still to upload, a few per frame.
    if (m_pictureInPicture) {
        drawPictureInPicture(frame, display);
    } else {
        drawInterface(frame, min, max, display);
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::Render();

    glViewport(0, 0, static_cast<GLsizei>(display.x), static_cast<GLsizei>(display.y));
    const ImVec4 clear = ImGui::ColorConvertU32ToFloat4(theme::kBackground);
    glClearColor(clear.x, clear.y, clear.z, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    m_display.present();

    const bool typing = ImGui::GetIO().WantTextInput;
    m_platform.setKeyboardVisible(typing);
    m_typing = typing;
    // ImGui hands queued input events out over several frames: keys typed in one burst.
    const bool inputQueued = !ImGui::GetCurrentContext()->InputEventsQueue.empty();
    return frame.animating || m_touch.down() || inputQueued;
}

void App::abandonFrame() {
    const ImGuiContext* context = ImGui::GetCurrentContext();
    if (context != nullptr && context->WithinFrameScope) {
        ImGui::EndFrame(); // Error recovery pops what the interrupted frame left pushed.
    }
    m_touch.setBlocked(false);
}

void App::drawInterface(Frame& frame, ImVec2 min, ImVec2 max, ImVec2 display) {
    // Modal sheets are drawn first on a top layer so they get input before the screen beneath,
    // which only sees touches that started outside them.
    ImDrawList* list = frame.canvas.list();
    const bool modal = m_sheet.modal();
    if (modal) {
        list->ChannelsSplit(2);
        list->ChannelsSetCurrent(1);
        drawSheet(frame, {min.x, max.y - modalSheetHeight(frame.canvas, max.y - min.y)}, max);
        list->ChannelsSetCurrent(0);
        m_touch.setBlocked(true);
    }
    // The mini player floats over everything else and claims the gestures that start on it,
    // including once the window has moved away from where the drag started.
    if (m_mini.watch && (m_mini.dragging || m_touch.startedIn(m_mini.min, m_mini.max))) {
        m_touch.setBlocked(true);
    }
    // Nothing under the launch animation reacts to touch.
    const bool splash = !m_splash.finished;
    if (splash) {
        m_touch.setBlocked(true);
    }
    if (m_stack.empty()) {
        drawTab(frame, min, max);
    } else {
        drawScreen(frame, m_stack.back(), min, max);
    }
    m_touch.setBlocked(modal || splash);
    if (m_mini.watch) {
        drawMiniPlayer(frame, min, max);
    }
    m_touch.setBlocked(splash);
    if (modal) {
        list->ChannelsMerge();
    }
    drawToast(frame, min, max);
    drawSplash(frame, display);
    m_touch.setBlocked(false);
}

// --- Screens ------------------------------------------------------------------------------------

void App::drawScreen(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max) {
    // Entry transition: pages slide in from the side, the watch page rises, both fading in.
    const float shown = motion::easeOutCubic(
        motion::progress(frame.time, screen.openedAt, motion::kScreenDuration));
    if (shown < 1) {
        frame.animating = true;
    }
    // Like the official app, the navigation bar stays under every page except the player and
    // sign-in.
    const bool navigation = screen.route != Route::Watch && screen.route != Route::SignIn;
    const float nav = navigation ? frame.dp(theme::kBottomBarHeight) : 0;
    ImVec2 screenMax{max.x, max.y - nav};
    float fade = 1;
    if (screen.route == Route::Watch) {
        // Dragging the video down slides the page away over the screen beneath, which is drawn
        // but not interactive.
        const float videoBottom = min.y + (max.x - min.x) * theme::kAspect16x9;
        const float pull =
            pullWatch(frame, min, {max.x, videoBottom - frame.dp(theme::kScrubZone)});
        if (pull > 0) {
            const bool blocked = m_touch.blocked();
            m_touch.setBlocked(true);
            if (m_stack.size() > 1) {
                drawScreen(frame, m_stack[m_stack.size() - 2], min, max);
            } else {
                drawTab(frame, min, max);
            }
            m_touch.setBlocked(blocked);
            fade = 1 - std::min(pull / (max.y - min.y), 1.0f);
            min.y += pull;
            screenMax.y += pull;
        }
    }
    motion::Layer layer(frame.canvas.list());
    switch (screen.route) {
    case Route::Search:
        drawSearch(frame, screen, min, screenMax);
        break;
    case Route::Watch:
        drawWatch(frame, screen, min, screenMax);
        break;
    case Route::Playlist:
    case Route::History:
        drawFeedScreen(frame, screen, min, screenMax);
        break;
    case Route::SignIn:
        drawSignIn(frame, min, screenMax);
        break;
    case Route::Channel:
        drawChannel(frame, screen, min, screenMax);
        break;
    case Route::Post:
        drawPost(frame, screen, min, screenMax);
        break;
    case Route::Downloads:
        drawDownloads(frame, screen, min, screenMax);
        break;
    case Route::Settings:
        drawSettings(frame, screen, min, screenMax);
        break;
    case Route::Notifications:
        drawNotifications(frame, screen, min, screenMax);
        break;
    case Route::AllSubscriptions:
        drawFeedScreen(frame, screen, min, screenMax);
        break;
    }
    const ImVec2 offset = screen.route == Route::Watch
                              ? ImVec2{0, (1 - shown) * (max.y - min.y) * kWatchRise}
                              : ImVec2{(1 - shown) * (max.x - min.x) * kScreenSlide, 0};
    layer.end(offset, shown * fade);
    if (navigation) {
        drawNavigationBar(frame, {min.x, screenMax.y}, max.x - min.x);
    }
}

void App::drawFeed(Frame& frame, std::shared_ptr<Feed> feed, ItemStyle style, ImVec2 min,
                   ImVec2 max, const ListHeader& header, std::span<const ListInsert> inserts) {
    if (feed->page.items.empty() && !header.draw) {
        if (feed->error.starts_with(http::kTransportError)) {
            drawOffline(frame, feed, min, max);
            return;
        }
        if (drawFeedStatus(frame, *feed, {min.x, min.y + m_listTop},
                           {max.x, max.y - m_listBottom}) &&
            feed->request) {
            load(feed, feed->request);
        }
        return;
    }
    // Under the tab bars, the list starts below the top bar and ends above the navigation bar.
    ListHeader padded = header;
    padded.top += m_listTop;
    padded.bottom += m_listBottom;
    std::optional<std::size_t> menu;
    if (const auto tapped = drawList(
            frame, *feed, style, min, max, padded, [this, feed] { loadMore(feed); }, inserts,
            &menu)) {
        later([this, item = feed->page.items[*tapped],
               source = VideoSource{.feed = feed, .index = *tapped}] { openItem(item, source); });
    }
    if (menu) {
        later([this, item = feed->page.items[*menu]] { openItemActions(item); });
    }
}

void App::drawTab(Frame& frame, ImVec2 min, ImVec2 max) {
    const float width = max.x - min.x;
    const float bar = frame.dp(theme::kTopBarHeight);
    const float nav = frame.dp(theme::kBottomBarHeight);
    const ImVec2 contentMin{min.x, min.y + bar};
    const ImVec2 contentMax{max.x, max.y - nav};

    // As the official app, scrolling a feed down slides both bars away and scrolling up brings
    // them back: the feed runs under them.
    const bool collapsing = m_tab != Tab::Shorts && m_session.signedIn();
    if (!collapsing || m_barTab != m_tab) {
        m_barShift = 0;
        m_barScroll = m_tabFeeds[std::to_underlying(m_tab)]->scroller.offset();
        m_barTab = m_tab;
    }
    const float topShift = m_barShift;
    const float navShift = bar > 0 ? m_barShift / bar * nav : 0;
    // Taps and drags starting on a bar are the bar's.
    Touch& touch = frame.touch;
    const bool wasBlocked = touch.blocked();
    const ImVec2 start = touch.start();
    const bool onBars = start.y < min.y + bar - topShift || start.y >= contentMax.y + navShift;
    if (collapsing && touch.down() && onBars) {
        touch.setBlocked(true);
    }

    const float shown =
        m_tabShownAt < 0 ? 1 : motion::progress(frame.time, m_tabShownAt, motion::kFadeDuration);
    if (shown < 1) {
        frame.animating = true;
    }
    motion::Layer layer(frame.canvas.list());
    if (collapsing) {
        m_listTop = bar;
        m_listBottom = nav;
    }
    const ImVec2 feedMin = collapsing ? min : contentMin;
    const ImVec2 feedMax = collapsing ? max : contentMax;
    switch (m_tab) {
    case Tab::Home:
    case Tab::Subscriptions:
        if (!m_session.signedIn()) {
            drawSignInPrompt(frame, contentMin, contentMax);
        } else if (m_tab == Tab::Subscriptions) {
            drawSubscriptions(frame, feedMin, feedMax);
        } else {
            auto shelves =
                m_tab == Tab::Home ? shortsShelves(frame.canvas, width) : std::vector<ListInsert>{};
            if (m_tab == Tab::Home) {
                loadPosts();
                addPostInserts(frame.canvas, width, shelves);
            }
            drawFeed(frame, m_tabFeeds[std::to_underlying(m_tab)], ItemStyle::Card, feedMin,
                     feedMax, {}, shelves);
        }
        break;
    case Tab::You:
        drawYou(frame, feedMin, feedMax);
        break;
    case Tab::Shorts:
        // Full bleed under the status bar, as in the official app.
        drawShorts(frame, {min.x, 0}, contentMax);
        break;
    }
    m_listTop = 0;
    m_listBottom = 0;
    layer.end({}, motion::easeOutCubic(shown));
    touch.setBlocked(wasBlocked);
    if (m_tab == Tab::Shorts) {
        drawNavigationBar(frame, {min.x, contentMax.y}, width);
        return;
    }

    if (collapsing) {
        const Scroller& scroller = m_tabFeeds[std::to_underlying(m_tab)]->scroller;
        const float offset = scroller.offset();
        // Follows the scroll, never hidden at the very top of the feed.
        m_barShift =
            std::clamp(m_barShift + offset - m_barScroll, 0.0f, std::clamp(offset, 0.0f, bar));
        m_barScroll = offset;
        // Once the feed rests, a bar half gone finishes hiding or showing.
        if (!touch.down() && !scroller.moving() && m_barShift > 0 && m_barShift < bar) {
            float hidden = m_barShift / bar;
            frame.animating |=
                motion::approach(hidden, hidden > 0.5f && offset >= bar ? 1 : 0, frame.seconds);
            m_barShift = hidden * bar;
        }
    }

    // Bars are drawn last so scrolled content slides underneath them; the top one slides under
    // the status bar, the navigation bar off the bottom.
    ImDrawList* list = frame.canvas.list();
    list->PushClipRect({min.x, min.y}, {max.x, max.y}, true);
    drawHomeBar(frame, {min.x, -topShift}, width);
    list->PopClipRect();
    frame.canvas.rect({min.x, 0}, {max.x, min.y}, theme::kBackground);
    list->PushClipRect({min.x, 0}, {max.x, max.y}, true);
    drawNavigationBar(frame, {min.x, contentMax.y + navShift}, width);
    list->PopClipRect();
    frame.canvas.rect({min.x, max.y}, {max.x, max.y + m_insets.bottom}, theme::kBackground);
}

void App::openSearch() {
    m_query.fill(0);
    m_focusSearch = true;
    m_filtersOpen = false;
    push(Screen{.route = Route::Search});
}

void App::drawHomeBar(Frame& frame, ImVec2 origin, float width) {
    const Canvas& c = frame.canvas;
    const float height = m_insets.top + c.dp(theme::kTopBarHeight);
    c.rect(origin, {origin.x + width, origin.y + height}, theme::kBackground);
    const float centerY = origin.y + m_insets.top + c.dp(theme::kTopBarHeight) * 0.5f;

    // Measured on the official app: the last icon 24 dp from the right edge, 48 dp between icons.
    float right = origin.x + width - c.dp(kBarIconInset);
    const auto nextIcon = [&] {
        const float x = right;
        right -= c.dp(kBarIconSpacing);
        return x;
    };
    if (m_tab == Tab::You) {
        // "Accounts" on the left (the account switcher), settings with the others.
        const Canvas medium = c.with(Weight::Medium);
        const std::string_view label = tr(Text::Accounts);
        const float pillHeight = c.dp(theme::kChipHeight);
        const float pillWidth =
            medium.measure(theme::kCaptionSize, label).x + c.dp(kAccountsPillExtra);
        const ImVec2 pillMin{origin.x + c.dp(theme::kGutter), centerY - pillHeight * 0.5f};
        const ImVec2 pillMax{pillMin.x + pillWidth, pillMin.y + pillHeight};
        drawPressed(frame, pillMin, pillMax, kChipRadius * 2);
        c.list()->AddRect(pillMin, pillMax, theme::kOutline, pillHeight * 0.5f, c.dp(1));
        medium.text({pillMin.x + c.dp(kAccountsPillPadding),
                     centerY - c.lineHeight(theme::kCaptionSize) * 0.5f},
                    theme::kCaptionSize, theme::kTextPrimary, label);
        c.icon(Icon::ChevronDown, {pillMax.x - c.dp(kAccountsPillPadding + 6), centerY}, 16,
               theme::kTextPrimary);
        if (frame.touch.tapped(pillMin, pillMax) && m_session.signedIn()) {
            later([this] { openSheet(Sheet::Accounts); });
        }
        if (iconButton(frame, Icon::Settings, {nextIcon(), centerY}, theme::kTextPrimary)) {
            later([this] { openSettings(); });
        }
    } else {
        const ImVec2 accent = c.measure(theme::kWordmarkSize, kWordmarkAccent);
        const ImVec2 at{origin.x + c.dp(theme::kGutter) + c.dp(4), centerY - accent.y * 0.5f};
        c.text(at, theme::kWordmarkSize, theme::kAccent, kWordmarkAccent);
        c.text({at.x + accent.x, at.y}, theme::kWordmarkSize, theme::kTextPrimary, kWordmarkRest);
    }
    if (iconButton(frame, Icon::Search, {nextIcon(), centerY}, theme::kTextPrimary)) {
        later([this] { openSearch(); });
    }
    if (m_session.signedIn()) {
        const ImVec2 bell{nextIcon(), centerY};
        if (iconButton(frame, Icon::Bell, bell, theme::kTextPrimary)) {
            later([this] { openNotifications(); });
        }
        drawUnreadBadge(c, bell, unreadNotifications());
    }
}

void App::drawTitleBar(Frame& frame, std::string_view title, ImVec2 origin, float width) {
    const Canvas& c = frame.canvas;
    const float height = m_insets.top + c.dp(theme::kTopBarHeight);
    c.rect(origin, {origin.x + width, origin.y + height}, theme::kBackground);
    const float centerY = origin.y + m_insets.top + c.dp(theme::kTopBarHeight) * 0.5f;
    const float icon = c.dp(theme::kTopBarHeight) * 0.5f;
    if (iconButton(frame, Icon::Back, {origin.x + icon, centerY}, theme::kTextPrimary)) {
        later([this] { goBack(); });
    }
    // Page titles are bold, as on the official app.
    const float textX = origin.x + icon * 2 + c.dp(theme::kGutter);
    c.with(theme::kHeadingWeight)
        .line({textX, centerY - c.lineHeight(theme::kSectionSize) * 0.5f},
              origin.x + width - textX - c.dp(theme::kGutter), theme::kSectionSize,
              theme::kTextPrimary, title);
}

void App::drawSignInPrompt(Frame& frame, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const float gutter = c.dp(theme::kGutter) * 2;
    const float width = max.x - min.x - gutter * 2;
    float y = min.y + (max.y - min.y) * 0.3f;
    y += c.paragraph({min.x + gutter, y}, width, theme::kBodySize, theme::kTextSecondary,
                     tr(Text::SignInPrompt), 4);
    y += c.dp(theme::kGutter);
    const float button = c.measure(theme::kBodySize, tr(Text::SignIn)).x + c.dp(40);
    const float x = (min.x + max.x - button) * 0.5f;
    if (pillButton(frame, tr(Text::SignIn), {x, y}, {x + button, y + c.dp(theme::kButtonHeight)},
                   true)) {
        later([this] { beginSignIn(); });
    }
}

void App::drawYou(Frame& frame, ImVec2 min, ImVec2 max) {
    if (!m_session.signedIn()) {
        drawSignInPrompt(frame, min, max);
        return;
    }
    const Canvas& c = frame.canvas;
    const float rowHeight = c.dp(48);
    const float shelf = shelfHeight(frame);
    const float spacing = c.dp(kSectionSpacing);
    const float chips = c.dp(theme::kChipHeight) + c.dp(theme::kGutter);

    // As the official app: the profile, "History ›" and its shelf, then "Library" with its chips
    // above the playlists.
    ListHeader header{
        .height = m_profileHeight + rowHeight + shelf + spacing + rowHeight + chips,
        .draw =
            [&](Frame& f, ImVec2 top, float width) {
                m_profileHeight = drawAccountHeader(f, top, width);
                const ImVec2 origin{top.x, top.y + m_profileHeight};
                if (textRow(f, tr(Text::History), origin, width)) {
                    later([this] {
                        Screen screen{.route = Route::History,
                                      .title = std::string(tr(Text::History))};
                        load(screen.feed, [this] { return m_api.feed(innertube::Feed::History); });
                        push(std::move(screen));
                    });
                }
                const ImVec2 shelfOrigin{origin.x, origin.y + rowHeight};
                std::optional<std::size_t> menu;
                if (m_historyShelf->page.items.empty()) {
                    drawFeedStatus(f, *m_historyShelf, shelfOrigin,
                                   {origin.x + width, shelfOrigin.y + shelf});
                } else if (const auto tapped =
                               drawShelf(f, *m_historyShelf, shelfOrigin, width, &menu)) {
                    later([this, item = m_historyShelf->page.items[*tapped],
                           source = VideoSource{.feed = m_historyShelf, .index = *tapped}] {
                        openItem(item, source);
                    });
                }
                if (menu) {
                    later([this, item = m_historyShelf->page.items[*menu]] {
                        openItemActions(item);
                    });
                }
                const float libraryY = shelfOrigin.y + shelf + spacing;
                const Canvas heading = f.canvas.with(theme::kHeadingWeight);
                const ImVec2 extent = heading.measure(theme::kSectionSize, tr(Text::Library));
                heading.text({origin.x + c.dp(theme::kShelfSpacing),
                              libraryY + (rowHeight - extent.y) * 0.5f},
                             theme::kSectionSize, theme::kTextPrimary, tr(Text::Library));
                // "Recent" keeps the server's order; "Playlists" sorts them by title.
                float x = origin.x + c.dp(theme::kShelfSpacing);
                const float chipsY = libraryY + rowHeight;
                for (const bool byTitle : {false, true}) {
                    const bool selected = m_libraryByTitle == byTitle;
                    if (chipButton(f, tr(byTitle ? Text::Playlists : Text::Recent), {x, chipsY},
                                   selected, x) &&
                        !selected) {
                        later([this, byTitle] { sortLibrary(byTitle); });
                    }
                }
            },
    };
    drawFeed(frame, m_tabFeeds[std::to_underlying(Tab::You)], ItemStyle::Row, min, max, header);
}

void App::sortLibrary(bool byTitle) {
    m_libraryByTitle = byTitle;
    auto& feed = *m_tabFeeds[std::to_underlying(Tab::You)];
    if (byTitle) {
        std::ranges::stable_sort(feed.page.items, {}, &innertube::Item::title);
    } else {
        feed = Feed{};
        for (const auto& [tab, kind] : kTabFeedSources) {
            if (tab == Tab::You) {
                load(m_tabFeeds[std::to_underlying(Tab::You)],
                     [this, feedKind = kind] { return m_api.feed(feedKind); });
            }
        }
    }
}

void App::drawSignIn(Frame& frame, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const float width = max.x - min.x;
    drawTitleBar(frame, tr(Text::SignIn), {min.x, 0}, width);

    const float gutter = c.dp(theme::kGutter) * 2;
    const float textWidth = width - gutter * 2;
    float y = min.y + c.dp(theme::kTopBarHeight) + gutter;
    const ImVec2 center{(min.x + max.x) * 0.5f, 0};

    if (m_signIn.failed) {
        c.paragraph({min.x + gutter, y}, textWidth, theme::kBodySize, theme::kTextSecondary,
                    tr(Text::SignInFailed), 3);
        y += c.dp(48);
        const float button = c.measure(theme::kBodySize, tr(Text::Retry)).x + c.dp(40);
        if (pillButton(frame, tr(Text::Retry), {center.x - button * 0.5f, y},
                       {center.x + button * 0.5f, y + c.dp(theme::kButtonHeight)}, true)) {
            later([this] {
                m_stack.pop_back();
                beginSignIn();
            });
        }
        return;
    }
    if (!m_signIn.code) {
        c.spinner({center.x, (min.y + max.y) * 0.5f}, theme::kSpinnerSize, theme::kTextSecondary,
                  frame.time);
        frame.animating = true;
        return;
    }

    const auto& code = *m_signIn.code;
    y += c.paragraph({min.x + gutter, y}, textWidth, theme::kBodySize, theme::kTextSecondary,
                     tr(Text::SignInStep), 3);
    const ImVec2 urlExtent = c.measure(theme::kTitleSize, code.verificationUrl);
    c.text({center.x - urlExtent.x * 0.5f, y + c.dp(8)}, theme::kTitleSize, theme::kLink,
           code.verificationUrl);
    y += c.dp(8) + urlExtent.y + c.dp(16);
    y += c.paragraph({min.x + gutter, y}, textWidth, theme::kBodySize, theme::kTextSecondary,
                     tr(Text::SignInCode), 2);

    const ImVec2 codeExtent = c.measure(theme::kCodeSize, code.userCode);
    y += c.dp(16);
    c.rect({center.x - codeExtent.x * 0.5f - c.dp(16), y - c.dp(8)},
           {center.x + codeExtent.x * 0.5f + c.dp(16), y + codeExtent.y + c.dp(8)}, theme::kSurface,
           theme::kCornerRadius);
    c.text({center.x - codeExtent.x * 0.5f, y}, theme::kCodeSize, theme::kTextPrimary,
           code.userCode);
    y += codeExtent.y + c.dp(40);

    const float button = c.measure(theme::kBodySize, tr(Text::OpenPage)).x + c.dp(40);
    if (pillButton(frame, tr(Text::OpenPage), {center.x - button * 0.5f, y},
                   {center.x + button * 0.5f, y + c.dp(theme::kButtonHeight)}, true)) {
        m_platform.openUrl(code.verificationUrl);
    }
    y += c.dp(theme::kButtonHeight) + c.dp(32);

    c.spinner({center.x, y}, theme::kSpinnerSize * 0.7f, theme::kTextSecondary, frame.time);
    frame.animating = true;
    const ImVec2 waitExtent = c.measure(theme::kCaptionSize, tr(Text::Waiting));
    c.text({center.x - waitExtent.x * 0.5f, y + c.dp(24)}, theme::kCaptionSize,
           theme::kTextSecondary, tr(Text::Waiting));
}

void App::drawFeedScreen(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max) {
    const float bar = frame.dp(theme::kTopBarHeight);
    drawFeed(frame, screen.feed, ItemStyle::Row, {min.x, min.y + bar + frame.dp(8)}, max);
    drawTitleBar(frame, screen.title, {min.x, 0}, max.x - min.x);
}

void App::drawWatch(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max) {
    // Fullscreen: the player covers the whole display and nothing else is drawn.
    if (m_fullscreen) {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        frame.canvas.rect({0, 0}, display, IM_COL32_BLACK);
        drawPlayer(frame, {0, 0}, display);
        return;
    }
    const Canvas& c = frame.canvas;
    const float width = max.x - min.x;
    const float videoHeight = width * theme::kAspect16x9;
    const ImVec2 belowVideo{min.x, min.y + videoHeight};
    c.rect({min.x, min.y - m_insets.top}, {max.x, belowVideo.y}, IM_COL32_BLACK);

    // Sheets cover everything below the player and take input first (top draw channel).
    ImDrawList* list = c.list();
    const bool sheet = m_sheet.kind == Sheet::Comments || m_sheet.kind == Sheet::Description;
    const bool blocked = m_touch.blocked();
    if (sheet && m_watch.info) {
        list->ChannelsSplit(2);
        list->ChannelsSetCurrent(1);
        drawSheet(frame, belowVideo, max);
        list->ChannelsSetCurrent(0);
        if (frame.touch.startedIn(belowVideo, max)) {
            m_touch.setBlocked(true);
        }
    }
    const bool covered =
        sheet && m_sheet.closingAt < 0 && frame.time - m_sheet.openedAt > motion::kSheetDuration;
    if (!covered) {
        ListHeader header{
            .height = m_watch.headerHeight,
            .draw = [this](Frame& f, ImVec2 origin,
                           float w) { m_watch.headerHeight = drawWatchHeader(f, origin, w); },
        };
        if (header.height == 0) {
            header.height = c.dp(theme::kTopBarHeight) * 4; // First frame estimate.
        }
        drawFeed(frame, screen.feed, ItemStyle::Card, belowVideo, max, header);
    }
    m_touch.setBlocked(blocked);
    drawPlayer(frame, min, {max.x, belowVideo.y});
    if (sheet && m_watch.info) {
        list->ChannelsMerge();
    }
}

void App::drawPlayer(Frame& frame, ImVec2 min, ImVec2 max) {
    constexpr float kButtonInset = 28; // dp from the player's edges to corner button centres.
    const Canvas& c = frame.canvas;
    ImDrawList* list = c.list();

    // Turning the phone sideways while watching goes fullscreen, as in the official app.
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (display.x <= display.y) {
        m_rotationEntersFullscreen = true;
    } else if (!m_fullscreen && m_rotationEntersFullscreen) {
        setFullscreen(true);
    }

    list->PushClipRect(min, max, true);
    if (m_fullscreen) {
        drawZoomableVideo(frame, min, max);
    } else {
        m_videoSurface->draw(list, min, max);
    }
    if (const auto error = m_subtitles->takeError()) {
        showToast(tr(*error));
    }
    // Subtitles rise above the controls while those are shown.
    m_subtitles->draw(c, min, max,
                      max.y -
                          c.dp(m_watch.controlsVisible ? kSubtitlesOverControls : kSubtitlesMargin),
                      m_fullscreen, m_player.positionUs());
    if (m_watch.menu != PlayerMenu::None) {
        drawPlayerMenu(frame, min, max);
        list->PopClipRect();
        return;
    }
    // A video that cannot play says so on the video itself, with a way to try again.
    if (!m_watch.error.empty() || m_player.state() == media::PlayerState::Failed) {
        const float gutter = c.dp(theme::kGutter);
        const float centerY = (min.y + max.y) * 0.5f;
        const float textHeight = c.paragraph(
            {min.x + gutter * 2, centerY - c.dp(theme::kButtonHeight)}, max.x - min.x - gutter * 4,
            theme::kBodySize, theme::kTextPrimary, tr(Text::CannotPlay), 2, TextAlign::Center);
        const float button = c.measure(theme::kBodySize, tr(Text::Retry)).x + c.dp(40);
        const ImVec2 buttonMin{(min.x + max.x - button) * 0.5f,
                               centerY - c.dp(theme::kButtonHeight) + textHeight + gutter};
        if (pillButton(frame, tr(Text::Retry), buttonMin,
                       {buttonMin.x + button, buttonMin.y + c.dp(theme::kButtonHeight)}, true)) {
            later([this] {
                m_watch.error.clear();
                m_videoSurface->release();
                m_player.close();
                startPlayback(m_watch.item.id);
            });
        }
        list->PopClipRect();
        return;
    }
    // A live stream's bar spans what it keeps, up to its edge.
    const auto live = liveTimeline();
    const auto durationMs =
        live ? live->durationMs : static_cast<std::int64_t>(m_player.durationUs() / 1000);
    const auto playedMs =
        live ? live->playedMs : static_cast<std::int64_t>(m_player.positionUs() / 1000);
    const float width = max.x - min.x;
    const auto toX = [&](std::int64_t ms) {
        return durationMs > 0 ? min.x + width * std::clamp(static_cast<float>(ms) /
                                                               static_cast<float>(durationMs),
                                                           0.0f, 1.0f)
                              : min.x;
    };
    const auto toMs = [&](float x) {
        return static_cast<std::int64_t>(std::clamp((x - min.x) / width, 0.0f, 1.0f) *
                                         static_cast<float>(durationMs));
    };

    // Easy seek: dragging the video while holding it for 2x seeks instead, with a pulse.
    if (m_speedHeld && durationMs > 0 && !m_watch.seek && m_touch.dragging() &&
        m_touch.startedIn(min, max)) {
        m_watch.seek = SeekState{.targetMs = playedMs,
                                 .fromMs = playedMs,
                                 .origin = m_touch.position(),
                                 .easy = true,
                                 .chapter = chapterAt(playedMs)};
        m_platform.haptic(Haptic::Seek);
    }
    if (holdForSpeed(frame, min, max, min.y + c.dp(kSpeedPillTop))) {
        m_watch.controlsVisible = false;
    }

    const auto state = m_player.state();
    const bool loading = state == media::PlayerState::Loading || m_player.buffering() ||
                         (state == media::PlayerState::Idle && m_watch.error.empty());
    const ImVec2 center{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
    const bool playing = state == media::PlayerState::Playing;

    if (playing && m_watch.controlsVisible && !m_watch.seek &&
        frame.time - m_watch.controlsShownAt > theme::kControlsTimeout) {
        m_watch.controlsVisible = false;
    }

    // Seeking owns drags that start near the bottom edge, and every drag of precise seeking.
    const ImVec2 scrubMin{min.x, max.y - c.dp(theme::kScrubZone)};
    if (!m_watch.seek && durationMs > 0 && m_touch.dragging() &&
        m_touch.startedIn(m_watch.fine ? min : scrubMin, max)) {
        const std::int64_t from = m_watch.fine ? m_watch.fineMs : playedMs;
        m_watch.seek = SeekState{.targetMs = from,
                                 .fromMs = playedMs,
                                 .origin = m_touch.start(),
                                 .chapter = chapterAt(from)};
    }
    if (m_watch.seek && m_touch.dragging()) {
        SeekState& seek = *m_watch.seek;
        const ImVec2 at = m_touch.position();
        const float pull = c.dp(kFinePull);
        if (m_watch.fine) {
            // The strip follows the finger: a frame's width is one storyboard interval.
            std::int64_t stepMs = innertube::storyboardInterval(
                m_watch.storyboard, static_cast<int>(c.dp(kFilmstripFrame)), durationMs);
            if (stepMs <= 0) {
                stepMs = kFineFrameMs;
            }
            seek.targetMs -= static_cast<std::int64_t>(
                m_touch.dragDelta().x / c.dp(kFilmstripFrame) * static_cast<float>(stepMs));
            if (at.y - seek.origin.y > pull) {
                closeFineSeek(m_watch.fineResume);
                seek.origin = at;
            }
        } else if (seek.easy) {
            seek.targetMs = seek.fromMs + static_cast<std::int64_t>((at.x - seek.origin.x) / width *
                                                                    static_cast<float>(durationMs));
        } else {
            seek.targetMs = toMs(at.x);
            if (seek.origin.y - at.y > pull && !live) { // Lives have no storyboard.
                m_watch.fine = true;
                m_watch.fineResume = playing;
                m_usedFineSeek = true;
                m_player.setPaused(true);
                seek.origin = at;
                m_platform.haptic(Haptic::Seek);
            }
        }
        seek.targetMs = std::clamp<std::int64_t>(seek.targetMs, 0, durationMs);
        if (m_watch.fine) {
            m_watch.fineMs = seek.targetMs;
        } else if (seek.undo.update(std::abs(toX(seek.targetMs) - toX(seek.fromMs)) / c.dp(1),
                                    frame.time)) {
            m_platform.haptic(Haptic::Click);
        }
        if (const std::size_t chapter = chapterAt(seek.targetMs); chapter != seek.chapter) {
            seek.chapter = chapter;
            m_platform.haptic(Haptic::Tick);
        }
        m_watch.controlsShownAt = frame.time;
        frame.animating = true;
    } else if (m_watch.seek) {
        if (!m_watch.seek->undo.armed(frame.time)) {
            if (live) {
                seekLive(m_watch.seek->targetMs);
            } else {
                m_player.seek(m_watch.seek->targetMs * 1000);
            }
        }
        m_watch.seek.reset();
        m_watch.shownPreview.reset();
    }
    if (m_watch.fine && !m_watch.seek) {
        m_watch.fineMs = std::clamp<std::int64_t>(m_watch.fineMs, 0, durationMs);
    }

    // While seeking, the official app shows the storyboard frame at the finger over the video,
    // with the time in a pill, and hides every other control.
    const bool seeking = m_watch.seek.has_value();
    const bool hideControls = seeking || m_watch.fine;
    const float barY = max.y - c.dp(theme::kProgressHeight);
    if (m_watch.fine) {
        drawFineSeek(frame, min, max);
    } else if (seeking) {
        const SeekState& seek = *m_watch.seek;
        const bool undo = seek.undo.armed(frame.time);
        // A live stream's time is how far behind live it is.
        std::string label;
        if (undo) {
            label = tr(Text::ReleaseToCancel);
        } else if (live) {
            label = '-' + formatDuration((durationMs - seek.targetMs) / 1000);
        }
        drawScrubPreview(frame, min, max, seek.targetMs, barY, label);
        if (!seek.easy && !undo && !m_usedFineSeek && !m_watch.storyboard.levels.empty()) {
            const float gutter = c.dp(theme::kGutter);
            c.line({min.x + gutter,
                    barY - c.dp(kTimePillGap) * 0.5f - c.lineHeight(theme::kCaptionSize) * 0.5f},
                   width - gutter * 2, theme::kCaptionSize, theme::kTextPrimary,
                   tr(Text::PullUpForPrecise), TextAlign::Center);
        }
    }
    // The autoplay countdown replaces the controls once the video has ended.
    const bool autoplaying = m_watch.endedAt >= 0 && m_settings.autoplay &&
                             !m_watch.autoplayCancelled && nextVideo().has_value();
    if (autoplaying) {
        drawAutoplay(frame, min, max);
    } else if (m_watch.controlsVisible && !hideControls) {
        c.rect(min, max, theme::kScrim);
        if (!loading &&
            iconButton(frame, playing ? Icon::Pause : Icon::Play, center, theme::kTextPrimary)) {
            m_player.setPaused(playing);
            if (state == media::PlayerState::Ended) {
                m_player.seek(0);
                m_player.setPaused(false);
            }
            m_watch.controlsShownAt = frame.time;
        }
        // Previous and next flank play/pause, greyed out when there is nowhere to go.
        const auto skipButton = [&](Icon icon, float direction, bool enabled) {
            const ImVec2 position{center.x + direction * c.dp(kSkipOffset), center.y};
            if (!enabled) {
                c.icon(icon, position, theme::kIconSize, kDisabledControl);
                return false;
            }
            return iconButton(frame, icon, position, theme::kTextPrimary);
        };
        if (skipButton(Icon::SkipPrevious, -1, !m_watchHistory.empty())) {
            later([this] { playPrevious(); });
        }
        const auto next = nextVideo();
        if (skipButton(Icon::SkipNext, 1, next.has_value())) {
            later([this, next] { openVideo(next->first, next->second); });
        }
    }
    // The bar: filled up to the playing position and split at chapters; while seeking, a larger
    // handle follows the finger and the fill stays where playback is.
    const std::span<const innertube::Chapter> chapters =
        m_watch.info ? std::span<const innertube::Chapter>(m_watch.info->chapters)
                     : std::span<const innertube::Chapter>();
    if ((m_watch.controlsVisible || seeking) && !m_watch.fine && durationMs > 0) {
        drawProgressBar(c, min.x, max.x, barY, max.y, toX(playedMs), chapters, durationMs,
                        seeking ? kSeekingChapterGap : kChapterGap);
        list->AddCircleFilled({seeking ? toX(m_watch.seek->targetMs) : toX(playedMs),
                               barY + c.dp(theme::kProgressHeight) * 0.5f},
                              c.dp(seeking ? theme::kScrubHandle : theme::kProgressHandle),
                              theme::kAccent);
    }
    if (m_watch.controlsVisible && !hideControls && !autoplaying) {
        // Controls stay clear of a display cutout beside a fullscreen video.
        const float left = min.x + (m_fullscreen ? m_insets.left : 0);
        const float right = max.x - (m_fullscreen ? m_insets.right : 0);
        float timeX = left + c.dp(theme::kGutter);
        std::string time;
        if (live) {
            // "LIVE" with a dot, red at the edge; behind it, grey with how far behind, and a tap
            // goes back to live, as in the official app.
            const float timeY = barY - c.dp(24) + c.lineHeight(theme::kCaptionSize) * 0.5f;
            list->AddCircleFilled({timeX + c.dp(kLiveDot) * 0.5f, timeY}, c.dp(kLiveDot) * 0.5f,
                                  live->atEdge ? theme::kAccent : theme::kTextSecondary);
            const ImVec2 tapMin{left, timeY - c.dp(theme::kChipHeight) * 0.5f};
            timeX += c.dp(kLiveDot + kLiveDotGap);
            time = tr(Text::Live);
            if (!live->atEdge) {
                time += "  -" + formatDuration((live->durationMs - live->playedMs) / 1000);
                const ImVec2 tapMax{timeX + c.measure(theme::kCaptionSize, time).x,
                                    timeY + c.dp(theme::kChipHeight) * 0.5f};
                if (m_touch.tapped(tapMin, tapMax)) {
                    seekLive(live->durationMs);
                    m_watch.controlsShownAt = frame.time;
                }
            }
        } else {
            time = formatDuration(playedMs / 1000) + " / " + formatDuration(durationMs / 1000);
            if (!chapters.empty()) {
                time += kChapterSeparator;
                time += chapters[chapterAt(playedMs)].title;
            }
        }
        // The chapter title runs up to the fullscreen button.
        c.line({timeX, barY - c.dp(24)}, right - c.dp(kButtonInset * 2) - timeX,
               theme::kCaptionSize, theme::kTextPrimary, time);

        if (iconButton(frame, Icon::Settings,
                       {right - c.dp(kButtonInset), min.y + c.dp(kButtonInset)},
                       theme::kTextPrimary)) {
            m_watch.menu = PlayerMenu::Settings;
            m_watch.menuScroller.reset();
        }
        // Greyed out while tracks load and for videos without any.
        const auto& subtitles = *m_subtitles;
        const ImVec2 captionsButton{right - c.dp(kButtonInset + kTopButtonSpacing),
                                    min.y + c.dp(kButtonInset)};
        if (subtitles.captions().tracks.empty()) {
            c.icon(Icon::Captions, captionsButton, theme::kIconSize, kDisabledControl);
        } else if (iconButton(frame, subtitles.selection() ? Icon::CaptionsOn : Icon::Captions,
                              captionsButton, theme::kTextPrimary)) {
            m_watch.menu = PlayerMenu::Subtitles;
            m_watch.menuScroller.reset();
        }
        if (iconButton(frame, m_fullscreen ? Icon::FullscreenExit : Icon::Fullscreen,
                       {right - c.dp(kButtonInset), barY - c.dp(18)}, theme::kTextPrimary)) {
            setFullscreen(!m_fullscreen);
            m_watch.controlsShownAt = frame.time;
        }
    }
    if (loading) {
        c.spinner(center, theme::kSpinnerSize * 1.4f, theme::kTextPrimary, frame.time);
        frame.animating = true;
    }
    list->PopClipRect();

    // Precise seeking keeps the video for itself: only its buttons react.
    if (m_touch.tapped(min, max) && !m_watch.fine) {
        m_watch.controlsVisible = !m_watch.controlsVisible;
        m_watch.controlsShownAt = frame.time;
    }
    // While playing, video frames wake the loop: the clock and the auto-hide follow them.
}

bool App::holdForSpeed(Frame& frame, ImVec2 min, ImVec2 max, float pillTop) {
    // A drag (scrubbing, pulling the video down) is not a hold: pressed() ends past the slop.
    const bool pressing = m_touch.pressed(min, max);
    // A broadcast plays at the edge: 2x would run out of stream within seconds.
    if (!m_speedHeld && pressing && m_player.state() == media::PlayerState::Playing &&
        !m_player.live()) {
        if (m_touch.pressDuration() >= kHoldSeconds) {
            m_speedHeld = true;
            m_player.setSpeed(kHeldSpeed);
            m_platform.haptic(Haptic::LongPress); // The official app vibrates as 2x starts.
        } else {
            frame.animating = true; // Wake up when the hold is long enough.
        }
    }
    if (m_speedHeld && !pressing) {
        m_speedHeld = false;
        m_player.setSpeed(m_speed);
        std::ignore = m_touch.tapped(min, max); // The release ends the hold, nothing else.
    }
    if (!m_speedHeld) {
        return false;
    }

    // "2x ▸▸" pill, top centre, as in the official app.
    drawPill(frame.canvas, (min.x + max.x) * 0.5f, pillTop, tr(Text::HeldSpeed), Icon::FastForward);
    frame.animating = true;
    return true;
}

void App::drawPill(const Canvas& c, float centreX, float top, std::string_view label,
                   std::optional<Icon> icon) {
    constexpr float kIcon = 16;   // dp
    constexpr float kHeight = 32; // dp
    const float gap = c.dp(4);
    const float padding = c.dp(theme::kGutter);
    const float textWidth = c.measure(theme::kBodySize, label).x;
    const float width = padding * 2 + textWidth + (icon ? gap + c.dp(kIcon) : 0);
    const ImVec2 pillMin{centreX - width * 0.5f, top};
    const ImVec2 pillMax{pillMin.x + width, top + c.dp(kHeight)};
    c.rect(pillMin, pillMax, theme::kBadge, theme::kPillRadius);
    const float centerY = (pillMin.y + pillMax.y) * 0.5f;
    c.text({pillMin.x + padding, centerY - c.lineHeight(theme::kBodySize) * 0.5f}, theme::kBodySize,
           theme::kTextPrimary, label);
    if (icon) {
        c.icon(*icon, {pillMin.x + padding + textWidth + gap + c.dp(kIcon) * 0.5f, centerY}, kIcon,
               theme::kTextPrimary);
    }
}

void App::drawZoomableVideo(Frame& frame, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const float fill = m_videoSurface->fillZoom(min, max);
    float zoom = m_zoom;
    bool pinching = false;
    if (m_touch.startedIn(min, max)) {
        if (const auto scale = m_touch.pinchScale()) {
            zoom = std::clamp(m_zoom * *scale, 1.0f, kMaxZoom);
            pinching = true;
            frame.animating = true;
        } else if (const auto ended = m_touch.pinchEnded()) {
            const float settled = m_zoom * *ended;
            if (settled < (1 + fill) * 0.5f) {
                m_zoom = 1;
            } else if (settled <= fill * kFillSnap) {
                m_zoom = fill;
                m_zoomedToFillAt = frame.time;
            } else {
                m_zoom = std::min(settled, kMaxZoom);
            }
            // The official app clicks as the video settles fitted or filling, not at a free zoom.
            if (m_zoom == 1 || m_zoom == fill) {
                m_platform.haptic(Haptic::Click);
            }
            zoom = m_zoom;
        }
    }
    m_videoSurface->draw(c.list(), min, max, zoom);

    const float centreX = (min.x + max.x) * 0.5f;
    const float top = min.y + c.dp(kZoomPillTop);
    const bool nearFill = zoom > (1 + fill) * 0.5f && zoom <= fill * kFillSnap;
    // A tick as the pinch reaches the zoom that snaps to fill.
    if (pinching && nearFill && !m_pinchNearFill) {
        m_platform.haptic(Haptic::Tick);
    }
    m_pinchNearFill = pinching && nearFill;
    if (pinching && !nearFill && zoom > 1) {
        // Circled factor, e.g. "2.7x".
        const float radius = c.dp(kZoomBadgeSize) * 0.5f;
        const ImVec2 centre{centreX, top + radius};
        c.list()->AddCircle(centre, radius, theme::kTextPrimary, 0, c.dp(kZoomBadgeStroke));
        const std::string label = std::format("{:.1f}x", zoom);
        const ImVec2 size = c.measure(theme::kBodySize, label);
        c.text({centre.x - size.x * 0.5f, centre.y - c.lineHeight(theme::kBodySize) * 0.5f},
               theme::kBodySize, theme::kTextPrimary, label);
    } else if ((pinching && nearFill) || (m_zoom == fill && m_zoomedToFillAt >= 0 &&
                                          frame.time - m_zoomedToFillAt < kZoomPillSeconds)) {
        drawPill(c, centreX, top, tr(Text::ZoomedToFill));
        frame.animating = true;
    }
}

void App::drawPlayerMenu(Frame& frame, ImVec2 min, ImVec2 max) {
    std::vector<MenuRow> rows;
    switch (m_watch.menu) {
    case PlayerMenu::None:
        return;
    case PlayerMenu::Settings: {
        // Quality, then speed (not for a live stream, which plays at the pace it is made).
        const auto qualities = m_player.qualities();
        const std::optional<std::size_t> current = m_player.currentQuality();
        std::string quality(tr(Text::Quality));
        if (current && *current < qualities.size()) {
            quality += " · ";
            if (m_player.autoQuality()) {
                quality +=
                    std::string(tr(Text::AutoQuality)) + " (" + qualities[*current].label + ')';
            } else {
                quality += qualities[*current].label;
            }
        }
        rows.push_back({std::move(quality), false, true});
        if (!m_player.live()) {
            rows.push_back(
                {std::string(tr(Text::PlaybackSpeed)) + " · " + speedLabel(m_speed), false, true});
        }
        if (const auto tapped = drawMenu(frame, min, max, tr(Text::Settings), rows)) {
            m_watch.menu = *tapped == 0 ? PlayerMenu::Quality : PlayerMenu::Speed;
            m_watch.menuScroller.reset();
        }
        break;
    }
    case PlayerMenu::Speed: {
        for (const float speed : kSpeeds) {
            rows.push_back({speedLabel(speed), speed == m_speed});
        }
        if (const auto tapped = drawMenu(frame, min, max, tr(Text::PlaybackSpeed), rows)) {
            m_speed = kSpeeds[*tapped];
            m_player.setSpeed(m_speed);
            m_watch.menu = PlayerMenu::None;
        }
        break;
    }
    case PlayerMenu::Quality: {
        const auto qualities = m_player.qualities();
        const std::optional<std::size_t> current = m_player.currentQuality();
        const bool automatic = m_player.autoQuality();
        MenuRow& autoRow = rows.emplace_back(std::string(tr(Text::AutoQuality)), automatic);
        if (automatic && current) {
            autoRow.label += " (" + qualities[*current].label + ')';
        }
        for (std::size_t index = 0; index < qualities.size(); ++index) {
            rows.push_back({qualities[index].label, !automatic && current == index});
        }
        if (const auto tapped = drawMenu(frame, min, max, tr(Text::Quality), rows)) {
            m_player.selectQuality(*tapped == 0 ? std::nullopt : std::optional{*tapped - 1});
            m_watch.menu = PlayerMenu::None;
        }
        break;
    }
    case PlayerMenu::Subtitles: {
        // Off, each track, then auto-translate when the server can translate.
        const auto& captions = m_subtitles->captions();
        const auto& selection = m_subtitles->selection();
        rows.push_back({std::string(tr(Text::SubtitlesOff)), !selection});
        for (std::size_t index = 0; index < captions.tracks.size(); ++index) {
            const auto& track = captions.tracks[index];
            std::string label = track.name;
            if (label.empty()) {
                label = track.language;
                if (track.autoGenerated) {
                    label += " (" + std::string(tr(Text::AutoGenerated)) + ')';
                }
            }
            rows.push_back({std::move(label), selection && selection->track == index &&
                                                  selection->translateTo.empty()});
        }
        const bool translatable =
            !captions.translations.empty() &&
            std::ranges::any_of(captions.tracks, &innertube::CaptionTrack::translatable);
        if (translatable) {
            MenuRow& translate =
                rows.emplace_back(std::string(tr(Text::AutoTranslate)), false, true);
            if (selection && !selection->translateTo.empty()) {
                translate.selected = true;
                for (const auto& language : captions.translations) {
                    if (language.code == selection->translateTo) {
                        translate.label += " (" + language.name + ')';
                    }
                }
            }
        }
        if (const auto tapped = drawMenu(frame, min, max, tr(Text::Subtitles), rows)) {
            if (*tapped == 0) {
                m_subtitles->select(std::nullopt);
            } else if (*tapped <= captions.tracks.size()) {
                m_subtitles->select(innertube::CaptionSelection{.track = *tapped - 1});
            } else {
                m_watch.menu = PlayerMenu::Translations;
                m_watch.menuScroller.reset();
                break;
            }
            m_watch.menu = PlayerMenu::None;
        }
        break;
    }
    case PlayerMenu::Translations: {
        const auto& captions = m_subtitles->captions();
        const auto& selection = m_subtitles->selection();
        for (const auto& language : captions.translations) {
            rows.push_back({language.name, selection && selection->translateTo == language.code});
        }
        if (const auto tapped = drawMenu(frame, min, max, tr(Text::AutoTranslate), rows)) {
            // Translate the track on screen, else the one in the language picked before.
            innertube::CaptionChoice choice = innertube::choiceOf(captions, selection);
            if (choice.language.empty()) {
                const auto source =
                    std::ranges::find_if(captions.tracks, &innertube::CaptionTrack::translatable);
                choice.language = source->language; // The menu row requires one.
            }
            choice.translateTo = captions.translations[*tapped].code;
            m_subtitles->select(innertube::resolve(captions, choice));
            m_watch.menu = PlayerMenu::None;
        }
        break;
    }
    }
}

std::optional<std::size_t> App::drawMenu(Frame& frame, ImVec2 min, ImVec2 max,
                                         std::string_view title, std::span<const MenuRow> rows) {
    constexpr float kRowHeight = 40; // dp
    constexpr float kWidth = 260;    // dp
    constexpr float kRadio = 8;      // dp, radio button radius.
    const Canvas& c = frame.canvas;
    c.rect(min, max, theme::kBadge);

    // A title row, then the rows, scrolling when they do not fit.
    const float gutter = c.dp(theme::kGutter);
    const float rowHeight = c.dp(kRowHeight);
    const float content = rowHeight * static_cast<float>(rows.size());
    const float height = std::min(rowHeight + content, max.y - min.y - gutter);
    const float width = std::min(c.dp(kWidth), max.x - min.x - gutter * 2);
    const ImVec2 panelMin{(min.x + max.x - width) * 0.5f, (min.y + max.y - height) * 0.5f};
    const ImVec2 panelMax{panelMin.x + width, panelMin.y + height};
    c.rect(panelMin, panelMax, theme::kSurfaceRaised, theme::kCornerRadius);

    const auto textY = [&](float rowTop, float size) {
        return rowTop + (rowHeight - c.lineHeight(size)) * 0.5f;
    };
    c.text({panelMin.x + gutter, textY(panelMin.y, theme::kCaptionSize)}, theme::kCaptionSize,
           theme::kTextSecondary, title);

    const ImVec2 listMin{panelMin.x, panelMin.y + rowHeight};
    m_watch.menuScroller.update(m_touch, listMin, panelMax, content, frame.seconds, c.dp(1));
    frame.animating |= m_watch.menuScroller.moving();
    ImDrawList* list = c.list();
    list->PushClipRect(listMin, panelMax, true);
    std::optional<std::size_t> tapped;
    for (std::size_t row = 0; row < rows.size(); ++row) {
        const float top =
            listMin.y + rowHeight * static_cast<float>(row) - m_watch.menuScroller.offset();
        if (top + rowHeight < listMin.y || top > panelMax.y) {
            continue;
        }
        const ImVec2 rowMin{panelMin.x, std::max(top, listMin.y)};
        const ImVec2 rowMax{panelMax.x, std::min(top + rowHeight, panelMax.y)};
        const MenuRow& entry = rows[row];
        drawPressed(frame, rowMin, rowMax);
        const ImVec2 radio{panelMin.x + gutter + c.dp(kRadio), top + rowHeight * 0.5f};
        if (entry.submenu) {
            c.icon(Icon::ChevronRight, {panelMax.x - gutter - c.dp(kRadio), radio.y},
                   theme::kIconSize, theme::kTextSecondary);
        }
        if (!entry.submenu || entry.selected) {
            list->AddCircle(radio, c.dp(kRadio),
                            entry.selected ? theme::kTextPrimary : theme::kTrack, 0,
                            c.dp(theme::kIconStroke));
        }
        if (entry.selected) {
            list->AddCircleFilled(radio, c.dp(kRadio) * 0.5f, theme::kTextPrimary);
        }
        const float labelX = radio.x + c.dp(kRadio) + gutter;
        c.line({labelX, textY(top, theme::kBodySize)},
               panelMax.x - labelX - gutter - (entry.submenu ? c.dp(kRadio * 2) : 0),
               theme::kBodySize, theme::kTextPrimary, entry.label);
        if (m_touch.tapped(rowMin, rowMax)) {
            tapped = row;
        }
    }
    list->PopClipRect();
    // Taps on the title are swallowed; taps outside the panel close it.
    if (!m_touch.tapped(panelMin, panelMax) && m_touch.tapped(min, max)) {
        m_watch.menu = PlayerMenu::None;
    }
    return tapped;
}

void App::setFullscreen(bool fullscreen) {
    m_fullscreen = fullscreen;
    m_platform.setFullscreen(fullscreen);
    if (!fullscreen) {
        m_rotationEntersFullscreen = false; // Until the display is seen in portrait again.
        m_zoom = 1;                         // Each fullscreen session starts fitted.
    }
}

} // namespace sloptube::app
