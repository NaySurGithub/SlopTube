// Host-side driver for the core library: exercises sign-in, feeds, search and stream resolution
// against the live service without needing a device.

#include "core/BuildInfo.hpp"
#include "core/auth/Session.hpp"
#include "core/http/Client.hpp"
#include "core/innertube/Api.hpp"
#include "core/innertube/Captions.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <print>
#include <span>
#include <string_view>
#include <thread>

namespace {

using namespace sloptube;

constexpr std::array<std::string_view, 5> kItemKindNames{"video", "short", "live", "channel",
                                                         "playlist"};

constexpr std::array<std::pair<std::string_view, innertube::Feed>, 4> kFeedCommands{{
    {"home", innertube::Feed::Home},
    {"subs", innertube::Feed::Subscriptions},
    {"history", innertube::Feed::History},
    {"playlists", innertube::Feed::Playlists},
}};

// Words of `search <query> [filter...]`, applied in order.
using FilterWord = std::pair<std::string_view, void (*)(innertube::SearchFilters&)>;
constexpr std::array<FilterWord, 18> kFilterWords{{
    {"popular", [](innertube::SearchFilters& f) { f.popular = true; }},
    {"hour", [](innertube::SearchFilters& f) { f.date = innertube::UploadDate::LastHour; }},
    {"today", [](innertube::SearchFilters& f) { f.date = innertube::UploadDate::Today; }},
    {"week", [](innertube::SearchFilters& f) { f.date = innertube::UploadDate::ThisWeek; }},
    {"month", [](innertube::SearchFilters& f) { f.date = innertube::UploadDate::ThisMonth; }},
    {"year", [](innertube::SearchFilters& f) { f.date = innertube::UploadDate::ThisYear; }},
    {"video", [](innertube::SearchFilters& f) { f.type = innertube::ResultType::Video; }},
    {"short", [](innertube::SearchFilters& f) { f.type = innertube::ResultType::Short; }},
    {"channel", [](innertube::SearchFilters& f) { f.type = innertube::ResultType::Channel; }},
    {"playlist", [](innertube::SearchFilters& f) { f.type = innertube::ResultType::Playlist; }},
    {"movie", [](innertube::SearchFilters& f) { f.type = innertube::ResultType::Movie; }},
    {"under3",
     [](innertube::SearchFilters& f) { f.duration = innertube::Duration::UnderThreeMinutes; }},
    {"medium",
     [](innertube::SearchFilters& f) { f.duration = innertube::Duration::ThreeToTwentyMinutes; }},
    {"long",
     [](innertube::SearchFilters& f) { f.duration = innertube::Duration::OverTwentyMinutes; }},
    {"live", [](innertube::SearchFilters& f) { f.live = true; }},
    {"hd", [](innertube::SearchFilters& f) { f.hd = true; }},
    {"4k", [](innertube::SearchFilters& f) { f.fourK = true; }},
    {"subtitles", [](innertube::SearchFilters& f) { f.subtitles = true; }},
}};

constexpr std::array<std::pair<std::string_view, innertube::ChannelTab>, 4> kTabNames{{
    {"videos", innertube::ChannelTab::Videos},
    {"shorts", innertube::ChannelTab::Shorts},
    {"live", innertube::ChannelTab::Live},
    {"playlists", innertube::ChannelTab::Playlists},
}};

std::filesystem::path sessionFile() {
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") / ".config" / build::kAppId / "session.json";
}

void usage() {
    std::println(
        stderr,
        "{} {}\n"
        "usage: sloptube-cli <command> [args]\n"
        "  login | logout\n"
        "  home | subs | history | playlists | shorts [pages]\n"
        "  search <query> [popular|hour|today|week|month|year|video|short|channel|playlist|\n"
        "    movie|under3|medium|long|live|hd|4k|subtitles...] | suggest <partial query>\n"
        "  playlist <id> [pages] | watch <video id> | comments <video id>\n"
        "  channel <channel id> | tab <channel id> videos|shorts|live|playlists [pages]\n"
        "  avatar <channel id> | accounts | chat <live video id> [polls]\n"
        "  posts [channel id] | post <channel id> [index]\n"
        "  player <video id> | captions <video id> [language [translate to]]",
        build::kAppName, build::kVersion);
}

template <typename T>
bool report(const Result<T>& result) {
    if (!result) {
        std::println(stderr, "error: {}", result.error().message);
    }
    return result.has_value();
}

int printPages(innertube::Api& api, Result<innertube::Page> page, int pages) {
    if (!report(page)) {
        return EXIT_FAILURE;
    }
    // Follows continuations as the app does, so repeated items are dropped the same way.
    for (int index = 1; index < pages && !page->continuation.empty(); ++index) {
        auto next = api.continuation(*page);
        if (!report(next)) {
            return EXIT_FAILURE;
        }
        innertube::appendPage(*page, std::move(*next));
    }
    for (const auto& item : page->items) {
        std::println("{:8} {:11} {:>8}  {}  —  {}  ·  {}",
                     kItemKindNames[std::to_underlying(item.kind)], item.id, item.badge, item.title,
                     item.channel, item.detail);
        if (item.kind == innertube::ItemKind::Channel) {
            std::println("{:8} avatar {}", "", item.avatar);
        }
    }
    return EXIT_SUCCESS;
}

int login(auth::Session& session) {
    auto code = session.beginSignIn();
    if (!code) {
        std::println(stderr, "error: {}", code.error().message);
        return EXIT_FAILURE;
    }
    std::println("Open {} and enter {}", code->verificationUrl, code->userCode);
    auto interval = code->pollInterval;
    while (true) {
        std::this_thread::sleep_for(interval);
        auto state = session.pollSignIn(*code);
        if (!state) {
            std::println(stderr, "error: {}", state.error().message);
            return EXIT_FAILURE;
        }
        switch (*state) {
        case auth::PollState::Pending:
            continue;
        case auth::PollState::SlowDown:
            interval *= 2;
            continue;
        case auth::PollState::Authorized:
            std::println("Signed in.");
            return EXIT_SUCCESS;
        case auth::PollState::Denied:
        case auth::PollState::Expired:
            std::println(stderr, "Sign-in was not completed.");
            return EXIT_FAILURE;
        }
    }
}

int player(innertube::Api& api, std::string_view videoId) {
    // VISIONOS answers only a client the server already knows (visitorData), as the app is once
    // it has loaded a feed.
    std::ignore = api.search(videoId);
    auto info = api.player(videoId);
    if (!info) {
        std::println(stderr, "error: {}", info.error().message);
        return EXIT_FAILURE;
    }
    if (info->details.live) {
        std::println("{} — {} (live)", info->details.title, info->details.channel);
    } else {
        std::println("{} — {} ({} s)", info->details.title, info->details.channel,
                     info->details.durationSeconds);
    }
    for (const auto& format : info->formats) {
        std::println("  itag {:3} {:>4}p{:<2} {:>8} kbps  {}{}", format.itag, format.height,
                     format.fps, format.bitrate / 1000, format.mimeType,
                     format.segmented() ? std::format("  ({} s segments)", format.segmentSeconds)
                                        : std::string());
    }
    if (auto storyboard = api.storyboard(videoId)) {
        info->storyboard = std::move(*storyboard);
    }
    for (const auto& level : info->storyboard.levels) {
        std::println("  storyboard {}x{} ×{} ({}×{} per sheet, {} ms)  {}", level.width,
                     level.height, level.count, level.columns, level.rows, level.intervalMs,
                     level.url);
    }
    return EXIT_SUCCESS;
}

constexpr std::size_t kPrintedCues = 12;

int captions(const http::Client& http, innertube::Api& api, std::string_view videoId,
             std::span<const std::string_view> choice) {
    auto list = api.captions(videoId);
    if (!list) {
        std::println(stderr, "error: {}", list.error().message);
        return EXIT_FAILURE;
    }
    for (std::size_t index = 0; index < list->tracks.size(); ++index) {
        const auto& track = list->tracks[index];
        std::println("{:2} {:8} {}{}{}", index, track.language, track.name,
                     track.autoGenerated ? " [auto-generated]" : "",
                     track.translatable ? " [translatable]" : "");
    }
    std::println("{} translation languages", list->translations.size());

    const innertube::CaptionChoice wanted{
        .language = std::string(choice.empty() ? "" : choice[0]),
        .translateTo = std::string(choice.size() > 1 ? choice[1] : ""),
    };
    auto selection = choice.empty() ? innertube::defaultSelection(*list, "fr")
                                    : innertube::resolve(*list, wanted);
    if (!selection && !list->tracks.empty() && choice.empty()) {
        selection = innertube::CaptionSelection{}; // Show something even without a French track.
    }
    if (!selection) {
        std::println("no matching track");
        return EXIT_SUCCESS;
    }
    const auto& track = list->tracks[selection->track];
    std::println("\nselected {} ({}){}", selection->track, track.name,
                 selection->translateTo.empty() ? "" : " -> " + selection->translateTo);
    auto cues = innertube::fetchCues(http, track, selection->translateTo);
    if (!cues) {
        std::println(stderr, "error: {}", cues.error().message);
        return EXIT_FAILURE;
    }
    std::println("{} cues", cues->size());
    for (std::size_t index = 0; index < std::min(cues->size(), kPrintedCues); ++index) {
        const auto& cue = (*cues)[index];
        std::println("[{:>7} ms] {}", cue.startMs,
                     innertube::captionText(*cues, cue.startMs + (cue.endMs - cue.startMs) / 2));
    }
    return EXIT_SUCCESS;
}

void printComments(const innertube::CommentPage& page, std::string_view indent) {
    for (const auto& comment : page.comments) {
        std::println("{}{} · {} · {} likes · {} replies\n{}  {}", indent, comment.author,
                     comment.published, comment.likes, comment.replyCount, indent, comment.text);
    }
}

int watch(innertube::Api& api, std::string_view videoId) {
    auto info = api.watch(videoId);
    if (!report(info)) {
        return EXIT_FAILURE;
    }
    const auto& channel = info->channel;
    std::println("{}\n{} · {}\n{} ({}) {} · subscribed: {} · avatar {}\nlikes {} (rating {})\n"
                 "comments {} (token: {})\n\n{}\n",
                 info->title, info->views, info->date, channel.name, channel.id,
                 channel.subscribers, channel.subscribed, channel.avatar, info->likes,
                 std::to_underlying(info->rating), info->commentCount, !info->commentsToken.empty(),
                 info->description);
    for (const auto& chapter : info->chapters) {
        std::println("chapter {:>8} ms  {}", chapter.startMs, chapter.title);
    }
    return printPages(api, std::move(info->related), 1);
}

int comments(innertube::Api& api, std::string_view videoId) {
    auto info = api.watch(videoId);
    if (!report(info)) {
        return EXIT_FAILURE;
    }
    auto page = api.comments(info->commentsToken);
    if (!report(page)) {
        return EXIT_FAILURE;
    }
    printComments(*page, "");
    for (const auto& comment : page->comments) {
        if (!comment.repliesToken.empty()) {
            std::println("\nreplies to {}:", comment.author);
            if (auto replies = api.comments(comment.repliesToken); report(replies)) {
                printComments(*replies, "    ");
            }
            break;
        }
    }
    return EXIT_SUCCESS;
}

void printPosts(std::span<const innertube::Post> posts) {
    for (const auto& post : posts) {
        std::println("{} · {} ({}) · {} · {} likes · {} comments", post.id, post.author,
                     post.channelId, post.published, post.likes, post.commentCount);
        std::println("  {}", post.text);
        for (const auto& image : post.images) {
            std::println("  image {}x{} {}", image.width, image.height, image.url);
        }
        for (const auto& choice : post.choices) {
            std::println("  choice {}", choice);
        }
        if (!post.votes.empty()) {
            std::println("  {}", post.votes);
        }
        if (post.video) {
            std::println("  video {} {} · {} · {}", post.video->id, post.video->title,
                         post.video->badge, post.video->detail);
        }
    }
}

// The `index`-th post of a channel's posts tab, opened like the post page: post, comments and
// the replies of the first thread that has some.
int post(innertube::Api& api, std::string_view channelId, int index) {
    auto page = api.channelPosts(channelId);
    if (!report(page)) {
        return EXIT_FAILURE;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= page->posts.size()) {
        std::println(stderr, "error: {} posts", page->posts.size());
        return EXIT_FAILURE;
    }
    auto detail = api.postDetail(page->posts[static_cast<std::size_t>(index)].detailParams);
    if (!report(detail)) {
        return EXIT_FAILURE;
    }
    printPosts(std::span(&*detail, 1));
    std::println("comments token: {}\n", !detail->commentsToken.empty());
    auto comments = api.postComments(detail->commentsToken);
    if (!report(comments)) {
        return EXIT_FAILURE;
    }
    printComments(*comments, "");
    std::println("more: {}", !comments->continuation.empty());
    for (const auto& comment : comments->comments) {
        if (!comment.repliesToken.empty()) {
            std::println("\nreplies to {}:", comment.author);
            if (auto replies = api.postComments(comment.repliesToken); report(replies)) {
                printComments(*replies, "    ");
            }
            break;
        }
    }
    return EXIT_SUCCESS;
}

int channel(innertube::Api& api, std::string_view channelId) {
    auto info = api.channel(channelId);
    if (!report(info)) {
        return EXIT_FAILURE;
    }
    const auto& summary = info->summary;
    std::println("{} {} · {} · subscribed: {}\navatar {}\nbanner {}", summary.name, summary.handle,
                 summary.subscribers, summary.subscribed, summary.avatar, info->banner);
    for (const auto& shelf : info->shelves) {
        std::println("\n[{}] {} items", shelf.title, shelf.items.size());
        for (const auto& item : shelf.items) {
            std::println("  {:11} {}", item.id, item.title);
        }
    }
    return EXIT_SUCCESS;
}

int chat(innertube::Api& api, std::string_view videoId, int polls) {
    auto channelId = api.videoChannel(videoId);
    if (!report(channelId)) {
        return EXIT_FAILURE;
    }
    std::string token = innertube::Api::liveChatToken(*channelId, videoId);
    for (int poll = 0; poll < polls && !token.empty(); ++poll) {
        auto page = api.liveChat(token);
        if (!report(page)) {
            return EXIT_FAILURE;
        }
        for (const auto& message : page->messages) {
            std::println("{}{}: {}", message.amount.empty() ? "" : "[" + message.amount + "] ",
                         message.author, message.text);
        }
        std::println("-- {} messages, next in {} ms", page->messages.size(), page->pollMs);
        token = page->continuation;
        if (poll + 1 < polls) {
            std::this_thread::sleep_for(std::chrono::milliseconds(page->pollMs));
        }
    }
    return EXIT_SUCCESS;
}

int run(std::span<const std::string_view> args) {
    if (args.empty()) {
        usage();
        return EXIT_FAILURE;
    }
    const http::Client http;
    auth::Session session(http, sessionFile());
    innertube::Api api(http, session, {.language = "fr", .region = "FR"});

    const auto command = args[0];
    const auto argument = args.size() > 1 ? args[1] : std::string_view{};
    const int pages = argument.empty() ? 1 : std::atoi(argument.data());

    if (command == "login") {
        return login(session);
    }
    if (command == "logout") {
        session.signOut();
        return EXIT_SUCCESS;
    }
    for (const auto& [name, feed] : kFeedCommands) {
        if (command == name) {
            return printPages(api, api.feed(feed), pages);
        }
    }
    if (command == "shorts") {
        return printPages(api, api.shorts(), pages);
    }
    if (command == "accounts") {
        auto accounts = api.accounts();
        if (!report(accounts)) {
            return EXIT_FAILURE;
        }
        for (const auto& account : *accounts) {
            std::println("{} · {} · {} · {}", account.name, account.handle, account.byline,
                         account.photo);
        }
        return EXIT_SUCCESS;
    }
    if (command == "posts") {
        if (argument.empty()) {
            auto posts = api.subscriptionPosts({});
            if (!report(posts)) {
                return EXIT_FAILURE;
            }
            printPosts(*posts);
            return EXIT_SUCCESS;
        }
        auto page = api.channelPosts(argument);
        if (!report(page)) {
            return EXIT_FAILURE;
        }
        printPosts(page->posts);
        return EXIT_SUCCESS;
    }
    if (argument.empty()) {
        usage();
        return EXIT_FAILURE;
    }
    if (command == "search") {
        innertube::SearchFilters filters;
        for (const auto word : args.subspan(2)) {
            const auto found = std::ranges::find(kFilterWords, word, &FilterWord::first);
            if (found == kFilterWords.end()) {
                usage();
                return EXIT_FAILURE;
            }
            found->second(filters);
        }
        return printPages(api, api.search(argument, filters), 1);
    }
    if (command == "suggest") {
        auto suggestions = api.searchSuggestions(argument);
        if (!report(suggestions)) {
            return EXIT_FAILURE;
        }
        for (const auto& suggestion : *suggestions) {
            std::println("{}", suggestion);
        }
        return EXIT_SUCCESS;
    }
    if (command == "tab") {
        const auto name = args.size() > 2 ? args[2] : std::string_view{};
        const auto found =
            std::ranges::find(kTabNames, name, &decltype(kTabNames)::value_type::first);
        if (found == kTabNames.end()) {
            usage();
            return EXIT_FAILURE;
        }
        return printPages(api, api.channelTab(argument, found->second),
                          args.size() > 3 ? std::atoi(args[3].data()) : 1);
    }
    if (command == "chat") {
        return chat(api, argument, args.size() > 2 ? std::atoi(args[2].data()) : 1);
    }
    if (command == "playlist") {
        return printPages(api, api.playlist(argument),
                          args.size() > 2 ? std::atoi(args[2].data()) : 1);
    }
    if (command == "watch") {
        return watch(api, argument);
    }
    if (command == "comments") {
        return comments(api, argument);
    }
    if (command == "channel") {
        return channel(api, argument);
    }
    if (command == "avatar") {
        // Accepts a channel id, or a video id whose uploader is looked up first.
        std::string channelId(argument);
        if (!channelId.starts_with("UC")) {
            auto uploader = api.videoChannel(argument);
            if (!report(uploader)) {
                return EXIT_FAILURE;
            }
            channelId = std::move(*uploader);
        }
        auto avatar = api.channelAvatar(channelId);
        if (!report(avatar)) {
            return EXIT_FAILURE;
        }
        std::println("{}", *avatar);
        return EXIT_SUCCESS;
    }
    if (command == "post") {
        return post(api, argument, args.size() > 2 ? std::atoi(args[2].data()) : 0);
    }
    if (command == "player") {
        return player(api, argument);
    }
    if (command == "captions") {
        return captions(http, api, argument, args.subspan(2));
    }
    usage();
    return EXIT_FAILURE;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    return run(args);
}
