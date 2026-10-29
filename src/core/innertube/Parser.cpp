#include "core/innertube/Parser.hpp"

#include <array>
#include <charconv>
#include <ranges>
#include <utility>

namespace sloptube::innertube::parser {
namespace {

using json::Element;

// Renderers that wrap sponsored content; never surfaced as items.
constexpr std::initializer_list<std::string_view> kAdContainers = {"adSlotRenderer",
                                                                   "adSlotAndLayoutRenderer"};

// Per-shelf horizontal pagination is not page-level pagination.
constexpr std::initializer_list<std::string_view> kNestedPagination = {"horizontalListRenderer"};

// Channel ids are the only browse ids with this prefix.
constexpr std::string_view kChannelIdPrefix = "UC";

constexpr std::string_view kCommentsPanel = "comment-item-section";
// Marker lists also carry key moments; only chapters split the progress bar.
constexpr std::string_view kChapterMarkers = "MARKER_TYPE_CHAPTERS";

// Post images come in several sizes; the smallest one at least this wide fills a card.
constexpr std::int64_t kPostImageMinWidth = 640;
constexpr std::array<std::string_view, 2> kPostVideoRenderers{"videoRenderer",
                                                              "compactVideoRenderer"};
constexpr std::string_view kMetaSeparator = " • ";

// Banners come in several widths; the smallest one at least this wide fills a phone screen.
constexpr std::int64_t kBannerMinWidth = 1080;

constexpr std::array<std::pair<std::string_view, Rating>, 2> kRatings{{
    {"LIKE", Rating::Like},
    {"DISLIKE", Rating::Dislike},
}};

constexpr std::array<std::pair<std::string_view, ItemKind>, 5> kTileKinds{{
    {"TILE_CONTENT_TYPE_VIDEO", ItemKind::Video},
    {"TILE_CONTENT_TYPE_SHORTS", ItemKind::Short},
    {"TILE_CONTENT_TYPE_LIVE", ItemKind::Live},
    {"TILE_CONTENT_TYPE_CHANNEL", ItemKind::Channel},
    {"TILE_CONTENT_TYPE_PLAYLIST", ItemKind::Playlist},
}};

constexpr std::array<std::pair<std::string_view, ItemKind>, 6> kLockupKinds{{
    {"LOCKUP_CONTENT_TYPE_VIDEO", ItemKind::Video},
    {"LOCKUP_CONTENT_TYPE_MUSIC", ItemKind::Video},
    {"LOCKUP_CONTENT_TYPE_SHORT", ItemKind::Short},
    {"LOCKUP_CONTENT_TYPE_LIVE", ItemKind::Live},
    {"LOCKUP_CONTENT_TYPE_CHANNEL", ItemKind::Channel},
    {"LOCKUP_CONTENT_TYPE_PLAYLIST", ItemKind::Playlist},
}};

template <std::size_t N>
std::optional<ItemKind>
lookupKind(const std::array<std::pair<std::string_view, ItemKind>, N>& table,
           std::string_view type) {
    for (const auto& [name, kind] : table) {
        if (name == type) {
            return kind;
        }
    }
    return std::nullopt;
}

// Flattens the three textual encodings InnerTube uses: simpleText, runs[] and content.
std::string text(Element parent, std::string_view pointer) {
    Element node;
    if (parent.at_pointer(pointer).get(node)) {
        return {};
    }
    if (const auto simple = json::string(node, "/simpleText"); !simple.empty()) {
        return std::string(simple);
    }
    if (const auto content = json::string(node, "/content"); !content.empty()) {
        return std::string(content);
    }
    std::string joined;
    simdjson::dom::array runs;
    if (!node["runs"].get_array().get(runs)) {
        for (const auto run : runs) {
            joined += json::string(run, "/text");
        }
    }
    return joined;
}

// Some images (MWEB, channel lockups) come with protocol-relative URLs ("//yt3.ggpht.com/...").
std::string absoluteUrl(std::string url) {
    if (url.starts_with("//")) {
        url.insert(0, "https:");
    }
    return url;
}

std::string lastThumbnail(Element parent, std::string_view pointer) {
    simdjson::dom::array list;
    if (parent.at_pointer(pointer).get_array().get(list) || list.size() == 0) {
        return {};
    }
    return absoluteUrl(std::string(json::string(list.at(list.size() - 1).value_unsafe(), "/url")));
}

Rating rating(std::string_view status) {
    for (const auto& [name, value] : kRatings) {
        if (name == status) {
            return value;
        }
    }
    return Rating::None;
}

std::string firstThumbnail(Element parent, std::string_view pointer) {
    return absoluteUrl(std::string(json::string(parent, std::string(pointer) + "/0/url")));
}

// First channel id any browse endpoint under `node` points to.
std::string linkedChannel(Element node) {
    std::string id;
    json::forEachKey(node, "browseEndpoint", [&](Element endpoint) {
        const auto browseId = json::string(endpoint, "/browseId");
        if (id.empty() && browseId.starts_with(kChannelIdPrefix)) {
            id = browseId;
        }
    });
    return id;
}

void subscribeButton(Element parent, ChannelSummary& channel) {
    json::forEachKey(parent, "subscribeButtonRenderer", [&](Element button) {
        channel.subscribed = json::boolean(button, "/subscribed");
        channel.canSubscribe = json::boolean(button, "/enabled");
        if (channel.id.empty()) {
            channel.id = json::string(button, "/channelId");
        }
        json::forEachKey(button, "subscribeEndpoint", [&](Element endpoint) {
            channel.subscribeParams = json::string(endpoint, "/params");
        });
    });
}

std::optional<Item> tile(Element node) {
    const auto kind = lookupKind(kTileKinds, json::string(node, "/contentType"));
    if (!kind) {
        return std::nullopt;
    }
    Item item{.kind = *kind, .id = std::string(json::string(node, "/contentId"))};

    Element metadata;
    if (!node.at_pointer("/metadata/tileMetadataRenderer").get(metadata)) {
        item.title = text(metadata, "/title");
        simdjson::dom::array lines;
        if (!metadata["lines"].get_array().get(lines)) {
            std::size_t index = 0;
            for (const auto line : lines) {
                std::string joined;
                simdjson::dom::array parts;
                if (!line.at_pointer("/lineRenderer/items").get_array().get(parts)) {
                    for (const auto part : parts) {
                        if (!joined.empty()) {
                            joined += ' ';
                        }
                        joined += text(part, "/lineItemRenderer/text");
                    }
                }
                (index++ == 0 ? item.channel : item.detail) = std::move(joined);
            }
        }
    }

    Element header;
    if (!node.at_pointer("/header/tileHeaderRenderer").get(header)) {
        item.thumbnail = lastThumbnail(header, "/thumbnail/thumbnails");
        json::forEachKey(header, "thumbnailOverlayTimeStatusRenderer",
                         [&](Element overlay) { item.badge = text(overlay, "/text"); });
    }
    if (item.kind == ItemKind::Channel) {
        item.avatar = item.thumbnail;
    } else {
        item.channelId = linkedChannel(node);
    }
    return item;
}

std::optional<Item> lockup(Element node) {
    const auto kind = lookupKind(kLockupKinds, json::string(node, "/contentType"));
    if (!kind) {
        return std::nullopt;
    }
    Item item{.kind = *kind, .id = std::string(json::string(node, "/contentId"))};

    Element metadata;
    if (!node.at_pointer("/metadata/lockupMetadataViewModel").get(metadata)) {
        item.title = std::string(json::string(metadata, "/title/content"));
        simdjson::dom::array rows;
        if (!metadata.at_pointer("/metadata/contentMetadataViewModel/metadataRows")
                 .get_array()
                 .get(rows)) {
            const auto delimiter =
                json::string(metadata, "/metadata/contentMetadataViewModel/delimiter");
            std::size_t index = 0;
            for (const auto row : rows) {
                std::string joined;
                simdjson::dom::array parts;
                if (!row["metadataParts"].get_array().get(parts)) {
                    for (const auto part : parts) {
                        if (!joined.empty()) {
                            joined += delimiter;
                        }
                        joined += json::string(part, "/text/content");
                    }
                }
                (index++ == 0 ? item.channel : item.detail) = std::move(joined);
            }
        }
    }

    Element image;
    if (!node.at_pointer("/contentImage/thumbnailViewModel").get(image)) {
        item.thumbnail = lastThumbnail(image, "/image/sources");
        json::forEachKey(image, "thumbnailBadgeViewModel", [&](Element badge) {
            item.badge = std::string(json::string(badge, "/text"));
        });
    }
    if (item.kind == ItemKind::Channel) {
        item.avatar = item.thumbnail;
    } else {
        item.channelId = linkedChannel(node);
        json::forEachKey(node, "decoratedAvatarViewModel", [&](Element avatar) {
            item.avatar = firstThumbnail(avatar, "/avatar/avatarViewModel/image/sources");
        });
    }
    return item;
}

void appendFormats(Element streamingData, std::string_view key, std::vector<Format>& formats) {
    simdjson::dom::array list;
    if (streamingData[key].get_array().get(list)) {
        return;
    }
    for (const auto entry : list) {
        auto url = json::string(entry, "/url");
        if (url.empty()) {
            continue; // Ciphered or SABR-only formats are not directly fetchable.
        }
        formats.push_back(Format{
            .itag = static_cast<int>(json::integer(entry, "/itag")),
            .url = std::string(url),
            .mimeType = std::string(json::string(entry, "/mimeType")),
            .width = static_cast<int>(json::integer(entry, "/width")),
            .height = static_cast<int>(json::integer(entry, "/height")),
            .fps = static_cast<int>(json::integer(entry, "/fps")),
            .bitrate = json::integer(entry, "/bitrate"),
            .contentLength = json::integer(entry, "/contentLength"),
            .qualityLabel = std::string(json::string(entry, "/qualityLabel")),
            .drc = json::boolean(entry, "/isDrc"),
            .defaultAudio = json::string(entry, "/audioTrack/id").empty() ||
                            json::boolean(entry, "/audioTrack/audioIsDefault"),
            .segmentSeconds = static_cast<int>(json::integer(entry, "/targetDurationSec")),
            .dvrSeconds = static_cast<int>(json::integer(entry, "/maxDvrDurationSec")),
        });
    }
}

// First `continuationCommand` token under `node` (MWEB and WEB continuations).
std::string continuationToken(Element node) {
    std::string token;
    json::forEachKey(node, "continuationCommand", [&](Element command) {
        if (token.empty()) {
            token = json::string(command, "/token");
        }
    });
    return token;
}

// Smallest rendition of an image at least kPostImageMinWidth wide, or the largest one.
PostImage postImage(Element image) {
    PostImage result;
    simdjson::dom::array list;
    if (image.at_pointer("/image/thumbnails").get_array().get(list)) {
        return result;
    }
    for (const auto entry : list) {
        result = PostImage{
            .url = absoluteUrl(std::string(json::string(entry, "/url"))),
            .width = static_cast<int>(json::integer(entry, "/width")),
            .height = static_cast<int>(json::integer(entry, "/height")),
        };
        if (result.width >= kPostImageMinWidth) {
            break;
        }
    }
    return result;
}

// Video attached to a post (compact or full video renderer).
Item attachedVideo(Element node) {
    Item item{
        .kind = ItemKind::Video,
        .id = std::string(json::string(node, "/videoId")),
        .title = text(node, "/title"),
        .channel = text(node, "/shortBylineText"),
        .badge = text(node, "/lengthText"),
        .thumbnail = lastThumbnail(node, "/thumbnail/thumbnails"),
        .channelId = linkedChannel(node),
    };
    auto views = text(node, "/shortViewCountText");
    auto date = text(node, "/publishedTimeText");
    item.detail = std::move(views);
    if (!item.detail.empty() && !date.empty()) {
        item.detail += kMetaSeparator;
    }
    item.detail += date;
    return item;
}

Post post(Element node) {
    Post result{
        .id = std::string(json::string(node, "/postId")),
        .channelId = std::string(json::string(node, "/authorEndpoint/browseEndpoint/browseId")),
        .author = text(node, "/authorText"),
        .avatar = absoluteUrl(lastThumbnail(node, "/authorThumbnail/thumbnails")),
        .published = text(node, "/publishedTimeText"),
        .text = text(node, "/contentText"),
        .likes = text(node, "/voteCount"),
        .commentCount = text(
            node, "/actionButtons/commentActionButtonsRenderer/replyButton/buttonRenderer/text"),
    };
    json::forEachKey(node, "browseEndpoint", [&](Element endpoint) {
        if (result.detailParams.empty() &&
            json::string(endpoint, "/browseId") == kPostDetailBrowseId) {
            result.detailParams = json::string(endpoint, "/params");
        }
    });

    Element attachment;
    if (node["backstageAttachment"].get(attachment)) {
        return result;
    }
    Element found;
    if (!attachment["backstageImageRenderer"].get(found)) {
        result.images.push_back(postImage(found));
    } else if (!attachment.at_pointer("/postMultiImageRenderer/images").get(found)) {
        json::forEachKey(found, "backstageImageRenderer",
                         [&](Element image) { result.images.push_back(postImage(image)); });
    } else if (!attachment["pollRenderer"].get(found)) {
        simdjson::dom::array choices;
        if (!found["choices"].get_array().get(choices)) {
            for (const auto choice : choices) {
                result.choices.push_back(text(choice, "/text"));
            }
        }
        result.votes = text(found, "/totalVotes");
    } else if (!attachment["quizViewModel"].get(found)) {
        json::forEachKey(found, "quizChoiceViewModel",
                         [&](Element choice) { result.choices.push_back(text(choice, "/text")); });
        result.votes = text(found, "/totalVotes");
    } else {
        for (const auto key : kPostVideoRenderers) {
            if (!attachment[key].get(found)) {
                result.video = attachedVideo(found);
                break;
            }
        }
    }
    return result;
}

} // namespace

PostPage posts(Element root) {
    PostPage result;
    json::forEachKey(root, "backstagePostRenderer", [&](Element node) {
        if (auto parsed = post(node); !parsed.id.empty()) {
            result.posts.push_back(std::move(parsed));
        }
    });
    // Outside the posts: the next page of a posts tab, or the comments of a post's own page.
    json::forEachKey(root, "continuationItemRenderer",
                     [&](Element node) {
                         if (result.continuation.empty()) {
                             result.continuation = continuationToken(node);
                         }
                     },
                     {"backstagePostRenderer"});
    return result;
}

Page page(Element root) {
    Page result;
    const auto visitItem = [&](Element node, auto&& convert) {
        if (auto item = convert(node); item && !item->id.empty()) {
            result.items.push_back(std::move(*item));
        }
    };
    // Tiles and lockups never coexist in one response, so two passes keep document order.
    json::forEachKey(root, "tileRenderer", [&](Element n) { visitItem(n, tile); }, kAdContainers);
    json::forEachKey(
        root, "lockupViewModel", [&](Element n) { visitItem(n, lockup); }, kAdContainers);

    json::forEachKey(
        root, "nextContinuationData",
        [&](Element node) {
            if (result.continuation.empty()) {
                result.continuation = json::string(node, "/continuation");
            }
        },
        kNestedPagination);
    return result;
}

Page mixQueue(Element root) {
    Page result;
    Element shelf;
    if (root.at_pointer("/contents/singleColumnWatchNextResults/pivot/sectionListRenderer/"
                        "contents/0/shelfRenderer")
            .get(shelf)) {
        return result;
    }
    json::forEachKey(shelf, "tileRenderer", [&](Element node) {
        if (auto item = tile(node);
            item && !item->id.empty() &&
            (item->kind == ItemKind::Video || item->kind == ItemKind::Live)) {
            result.items.push_back(std::move(*item));
        }
    });
    return result;
}

Storyboard storyboard(std::string_view spec) {
    // "<base url>|<level 0>|<level 1>…", each level "w#h#count#columns#rows#interval#name#sigh";
    // the base URL holds "$L" (level) and "$N" (name, which may hold "$M", the sheet).
    Storyboard result;
    const auto split = [](std::string_view text, char separator) {
        std::vector<std::string_view> parts;
        for (const auto part : std::views::split(text, separator)) {
            parts.emplace_back(part.begin(), part.end());
        }
        return parts;
    };
    const auto parts = split(spec, '|');
    if (parts.size() < 2) {
        return result;
    }
    const auto number = [](std::string_view text) {
        std::int64_t value = 0;
        std::from_chars(text.data(), text.data() + text.size(), value);
        return value;
    };
    const auto replace = [](std::string text, std::string_view from, std::string_view to) {
        if (const auto at = text.find(from); at != std::string::npos) {
            text.replace(at, from.size(), to);
        }
        return text;
    };
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const auto fields = split(parts[i], '#');
        if (fields.size() < 8) {
            continue;
        }
        std::string url =
            replace(replace(std::string(parts[0]), "$L", std::to_string(i - 1)), "$N", fields[6]);
        url += "&sigh=";
        url += fields[7];
        result.levels.push_back({
            .width = static_cast<int>(number(fields[0])),
            .height = static_cast<int>(number(fields[1])),
            .count = static_cast<int>(number(fields[2])),
            .columns = static_cast<int>(number(fields[3])),
            .rows = static_cast<int>(number(fields[4])),
            .intervalMs = number(fields[5]),
            .url = std::move(url),
        });
    }
    return result;
}

Result<Player> player(Element root) {
    const auto status = json::string(root, "/playabilityStatus/status");
    if (status != "OK") {
        const auto reason = json::string(root, "/playabilityStatus/reason");
        return fail(std::string(status) + ": " + std::string(reason));
    }

    Player result;
    Element details;
    if (!root["videoDetails"].get(details)) {
        result.details = VideoDetails{
            .id = std::string(json::string(details, "/videoId")),
            .title = std::string(json::string(details, "/title")),
            .channel = std::string(json::string(details, "/author")),
            .channelId = std::string(json::string(details, "/channelId")),
            .durationSeconds = json::integer(details, "/lengthSeconds"),
            .viewCount = json::integer(details, "/viewCount"),
            .description = std::string(json::string(details, "/shortDescription")),
            .live = json::boolean(details, "/isLive"),
        };
    }

    Element streamingData;
    if (!root["streamingData"].get(streamingData)) {
        appendFormats(streamingData, "formats", result.formats);
        appendFormats(streamingData, "adaptiveFormats", result.formats);
    }
    if (result.formats.empty()) {
        return fail("player: no directly playable formats");
    }

    result.storyboard =
        storyboard(json::string(root, "/storyboards/playerStoryboardSpecRenderer/spec"));
    result.playbackTrackingUrl =
        json::string(root, "/playbackTracking/videostatsPlaybackUrl/baseUrl");
    result.watchtimeTrackingUrl =
        json::string(root, "/playbackTracking/videostatsWatchtimeUrl/baseUrl");
    return result;
}

Captions captions(Element root) {
    Captions result;
    Element list;
    if (root.at_pointer("/captions/playerCaptionsTracklistRenderer").get(list)) {
        return result;
    }
    simdjson::dom::array entries;
    if (!list["captionTracks"].get_array().get(entries)) {
        for (const auto entry : entries) {
            auto url = json::string(entry, "/baseUrl");
            if (url.empty()) {
                continue;
            }
            result.tracks.push_back(CaptionTrack{
                .url = std::string(url),
                .language = std::string(json::string(entry, "/languageCode")),
                .name = text(entry, "/name"),
                .autoGenerated = json::string(entry, "/kind") == "asr",
                .translatable = json::boolean(entry, "/isTranslatable"),
            });
        }
    }
    if (!list["translationLanguages"].get_array().get(entries)) {
        for (const auto entry : entries) {
            result.translations.push_back(CaptionLanguage{
                .code = std::string(json::string(entry, "/languageCode")),
                .name = text(entry, "/languageName"),
            });
        }
    }
    return result;
}

Watch watch(Element root) {
    Watch result{.related = page(root)};
    result.related.source = Endpoint::Next;

    json::forEachKey(root, "videoMetadataRenderer", [&](Element metadata) {
        result.videoId = json::string(metadata, "/videoId");
        result.title = text(metadata, "/title");
        result.views = text(metadata, "/viewCount/videoViewCountRenderer/shortViewCount");
        if (result.views.empty()) {
            result.views = text(metadata, "/viewCount/videoViewCountRenderer/viewCount");
        }
        result.date = text(metadata, "/dateText");

        Element owner;
        if (!metadata.at_pointer("/owner/videoOwnerRenderer").get(owner)) {
            auto& channel = result.channel;
            channel.id = json::string(owner, "/navigationEndpoint/browseEndpoint/browseId");
            channel.name = text(owner, "/title");
            channel.avatar = lastThumbnail(owner, "/thumbnail/thumbnails");
            channel.subscribers = text(owner, "/subscriberCountText");
            subscribeButton(owner, channel);
        }
    });

    json::forEachKey(root, "likeButtonRenderer", [&](Element like) {
        result.rating = rating(json::string(like, "/likeStatus"));
        result.likes = text(like, "/likeCountText");
        result.likesAllowed = json::boolean(like, "/likesAllowed");
    });

    json::forEachKey(root, "expandableVideoDescriptionBodyRenderer", [&](Element body) {
        result.description = text(body, "/descriptionBodyText");
    });

    json::forEachKey(root, "macroMarkersListEntity", [&](Element entity) {
        simdjson::dom::array markers;
        if (json::string(entity, "/markersList/markerType") != kChapterMarkers ||
            entity.at_pointer("/markersList/markers").get_array().get(markers)) {
            return;
        }
        result.chapters.clear();
        for (const auto marker : markers) {
            result.chapters.push_back({.title = text(marker, "/title"),
                                       .startMs = json::integer(marker, "/startMillis")});
        }
    });

    json::forEachKey(root, "engagementPanelSectionListRenderer", [&](Element panel) {
        if (json::string(panel, "/panelIdentifier") != kCommentsPanel) {
            return;
        }
        Element header;
        if (panel.at_pointer("/header/engagementPanelTitleHeaderRenderer").get(header)) {
            return;
        }
        result.commentCount = text(header, "/contextualInfo");
        // Sort options: top comments first, then newest.
        simdjson::dom::array sorts;
        if (!header.at_pointer("/menu/sortFilterSubMenuRenderer/subMenuItems")
                 .get_array()
                 .get(sorts)) {
            std::size_t index = 0;
            for (const auto sort : sorts) {
                auto token = std::string(
                    json::string(sort, "/continuation/reloadContinuationData/continuation"));
                (index++ == 0 ? result.commentsToken : result.newestCommentsToken) =
                    std::move(token);
            }
        }
        if (result.commentsToken.empty()) {
            json::forEachKey(panel, "reloadContinuationData", [&](Element data) {
                if (result.commentsToken.empty()) {
                    result.commentsToken = json::string(data, "/continuation");
                }
            });
        }
    });
    return result;
}

CommentPage comments(Element root) {
    CommentPage result;
    json::forEachKey(root, "commentRenderer", [&](Element node) {
        Comment comment{
            .id = std::string(json::string(node, "/commentId")),
            .author = text(node, "/authorText"),
            .avatar = lastThumbnail(node, "/authorThumbnail/thumbnails"),
            .text = text(node, "/contentText"),
            .published = text(node, "/publishedTimeText"),
            .likes = text(node, "/voteCount"),
            .rating = rating(json::string(node, "/voteStatus")),
            .replyCount = json::integer(node, "/replyCount"),
        };
        if (comment.replyCount > 0) {
            json::forEachKey(node, "reloadContinuationData", [&](Element data) {
                comment.repliesToken = json::string(data, "/continuation");
            });
        }
        result.comments.push_back(std::move(comment));
    });
    json::forEachKey(root, "nextContinuationData", [&](Element node) {
        if (result.continuation.empty()) {
            result.continuation = json::string(node, "/continuation");
        }
    });

    // MWEB (post comments): replies hang off the thread beside the comment, and pages continue
    // through continuation items rather than continuation data.
    json::forEachKey(root, "commentThreadRenderer", [&](Element thread) {
        const auto id = json::string(thread, "/comment/commentRenderer/commentId");
        for (auto& comment : result.comments) {
            if (comment.id == id && comment.repliesToken.empty()) {
                json::forEachKey(thread, "commentRepliesRenderer", [&](Element replies) {
                    comment.repliesToken = continuationToken(replies);
                });
            }
        }
    });
    if (result.continuation.empty()) {
        json::forEachKey(root, "continuationItemRenderer",
                         [&](Element node) {
                             if (result.continuation.empty()) {
                                 result.continuation = continuationToken(node);
                             }
                         },
                         {"commentRepliesRenderer"});
    }
    return result;
}

Channel channel(Element root) {
    Channel result;
    json::forEachKey(root, "channelHeaderRenderer", [&](Element header) {
        auto& summary = result.summary;
        summary.name = text(header, "/title");
        summary.avatar = lastThumbnail(header, "/avatar/thumbnails");

        simdjson::dom::array banners;
        if (!header.at_pointer("/backgroundImage/thumbnails").get_array().get(banners)) {
            for (const auto banner : banners) {
                result.banner = json::string(banner, "/url");
                if (json::integer(banner, "/width") >= kBannerMinWidth) {
                    break;
                }
            }
        }

        // Subtitle items: "@handle", separators, subscriber count.
        simdjson::dom::array parts;
        if (!header.at_pointer("/subtitle/lineRenderer/items").get_array().get(parts)) {
            for (const auto part : parts) {
                auto value = text(part, "/lineItemRenderer/text");
                if (value.starts_with('@')) {
                    summary.handle = std::move(value);
                } else if (value.size() > 3) { // Skips "•" separators.
                    summary.subscribers = std::move(value);
                }
            }
        }
        subscribeButton(header, summary);
    });

    json::forEachKey(root, "shelfRenderer", [&](Element node) {
        Shelf shelf{
            .title = text(
                node,
                "/headerRenderer/shelfHeaderRenderer/avatarLockup/avatarLockupRenderer/title")};
        json::forEachKey(node, "tileRenderer", [&](Element tileNode) {
            if (auto item = tile(tileNode); item && !item->id.empty()) {
                shelf.items.push_back(std::move(*item));
            }
        });
        if (!shelf.items.empty()) {
            result.shelves.push_back(std::move(shelf));
        }
    });
    return result;
}

std::vector<Account> accounts(Element root) {
    std::vector<Account> result;
    json::forEachKey(root, "accountItem", [&](Element node) {
        result.push_back(Account{
            .name = text(node, "/accountName"),
            .byline = text(node, "/accountByline"),
            .handle = text(node, "/channelHandle"),
            .photo = lastThumbnail(node, "/accountPhoto/thumbnails"),
        });
    });
    return result;
}

std::vector<PlaylistOption> saveOptions(Element root) {
    std::vector<PlaylistOption> result;
    json::forEachKey(root, "playlistAddToOptionRenderer", [&](Element node) {
        PlaylistOption option{
            .id = std::string(json::string(node, "/playlistId")),
            .title = text(node, "/title"),
            // "ALL" or "SOME" (several videos) both count as saved.
            .contains = json::string(node, "/containsSelectedVideos") != "NONE",
        };
        const auto privacy = json::string(node, "/privacy");
        for (std::size_t i = 0; i < kPrivacyNames.size(); ++i) {
            if (privacy == kPrivacyNames[i]) {
                option.privacy = static_cast<Privacy>(i);
            }
        }
        if (!option.id.empty()) {
            result.push_back(std::move(option));
        }
    });
    return result;
}

std::string channelAvatar(Element root) {
    std::string avatar;
    json::forEachKey(root, "decoratedAvatarViewModel", [&](Element node) {
        if (avatar.empty()) {
            avatar = firstThumbnail(node, "/avatar/avatarViewModel/image/sources");
        }
    });
    if (avatar.empty()) {
        json::forEachKey(root, "channelHeaderRenderer",
                         [&](Element node) { avatar = lastThumbnail(node, "/avatar/thumbnails"); });
    }
    return avatar;
}

std::string_view videoChannel(Element root) {
    return json::string(root, "/videoDetails/channelId");
}

void videoDetails(Element root, Item& item) {
    item.title = json::string(root, "/videoDetails/title");
    item.channel = json::string(root, "/videoDetails/author");
    item.channelId = json::string(root, "/videoDetails/channelId");
}

Page reelSequence(Element root) {
    Page page;
    json::forEachKey(root, "reelWatchEndpoint", [&](Element endpoint) {
        Item item{.kind = ItemKind::Short, .id = std::string(json::string(endpoint, "/videoId"))};
        if (item.id.empty()) {
            return;
        }
        item.thumbnail = json::string(endpoint, "/thumbnail/thumbnails/0/url");
        page.items.push_back(std::move(item));
    });
    page.continuation = json::string(root, "/continuationEndpoint/continuationCommand/token");
    return page;
}

namespace {

// The URL suffix of each ChannelTab, as the selected tab of an MWEB response names it.
constexpr std::array<std::string_view, 4> kChannelTabPaths{
    "/videos",
    "/shorts",
    "/streams",
    "/playlists",
};
static_assert(kChannelTabPaths.size() == std::to_underlying(ChannelTab::Playlists) + 1);

// The selected tab of an MWEB channel response is `tab`.
bool showsTab(Element root, ChannelTab tab) {
    bool shown = false;
    json::forEachKey(root, "tabRenderer", [&](Element renderer) {
        if (json::boolean(renderer, "/selected")) {
            shown = json::string(renderer, "/endpoint/commandMetadata/webCommandMetadata/url")
                        .ends_with(kChannelTabPaths[std::to_underlying(tab)]);
        }
    });
    return shown;
}

// MWEB channel videos and lives (`videoWithContextRenderer`).
Item contextVideo(Element node) {
    Item item{
        .id = std::string(json::string(node, "/videoId")),
        .title = text(node, "/headline"),
        .channelId = linkedChannel(node),
    };
    item.badge = text(node, "/lengthText");
    json::forEachKey(node, "thumbnailOverlayTimeStatusRenderer", [&](Element overlay) {
        if (json::string(overlay, "/style") == "LIVE") {
            item.kind = ItemKind::Live;
            item.badge = text(overlay, "/text");
        }
    });
    item.detail = text(node, "/shortViewCountText");
    if (const auto published = text(node, "/publishedTimeText"); !published.empty()) {
        item.detail += item.detail.empty() ? "" : kMetaSeparator;
        item.detail += published;
    }
    return item;
}

// MWEB channel shorts (`shortsLockupViewModel`). The title only comes inside the accessibility
// text, "<title>, <views> - <action>", which is cut at its last separators.
Item shortsLockup(Element node) {
    Item item{
        .kind = ItemKind::Short,
        .id = std::string(json::string(node, "/onTap/innertubeCommand/reelWatchEndpoint/videoId")),
        .detail = std::string(json::string(node, "/overlayMetadata/secondaryText/content")),
    };
    std::string_view label = json::string(node, "/accessibilityText");
    if (const auto action = label.rfind(" - "); action != std::string_view::npos) {
        label = label.substr(0, action);
    }
    if (const auto views = label.rfind(", "); views != std::string_view::npos) {
        label = label.substr(0, views);
    }
    item.title = label;
    return item;
}

// MWEB channel playlists (`compactPlaylistRenderer`).
Item compactPlaylist(Element node) {
    return Item{
        .kind = ItemKind::Playlist,
        .id = std::string(json::string(node, "/playlistId")),
        .title = text(node, "/title"),
        .channel = text(node, "/shortBylineText"),
        .detail = text(node, "/publishedTimeText"),
        .badge = text(node, "/videoCountShortText"),
        .thumbnail = lastThumbnail(node, "/thumbnail/thumbnails"),
    };
}

} // namespace

Page channelTab(Element root, std::optional<ChannelTab> tab) {
    Page page;
    if (tab && !showsTab(root, *tab)) {
        return page;
    }
    const auto add = [&](Item item) {
        if (!item.id.empty()) {
            page.items.push_back(std::move(item));
        }
    };
    // Header chips and the about panel carry continuations too; only the list's own last entry
    // continues it.
    json::forEachKey(root, "richItemRenderer",
                     [&](Element entry) {
                         Element content;
                         if (entry["content"].get(content)) {
                             return;
                         }
                         Element node;
                         if (!content["videoWithContextRenderer"].get(node)) {
                             add(contextVideo(node));
                         } else if (!content["shortsLockupViewModel"].get(node)) {
                             add(shortsLockup(node));
                         }
                     },
                     {"header"});
    json::forEachKey(root, "compactPlaylistRenderer",
                     [&](Element node) { add(compactPlaylist(node)); }, {"header"});
    json::forEachKey(root, "continuationItemRenderer",
                     [&](Element node) {
                         page.continuation =
                             json::string(node, "/continuationEndpoint/continuationCommand/token");
                     },
                     {"header", "chipBarViewModel"});
    return page;
}

ChatPage liveChat(Element root) {
    ChatPage page;
    Element chat;
    if (root.at_pointer("/continuationContents/liveChatContinuation").get(chat)) {
        return page;
    }
    simdjson::dom::array actions;
    if (!chat["actions"].get_array().get(actions)) {
        for (const auto action : actions) {
            Element item;
            if (action.at_pointer("/addChatItemAction/item").get(item)) {
                continue;
            }
            Element message;
            const bool paid = !item["liveChatPaidMessageRenderer"].get(message);
            if (!paid && item["liveChatTextMessageRenderer"].get(message)) {
                continue; // Engagement notices, gifts and memberships.
            }
            ChatMessage entry{
                .id = std::string(json::string(message, "/id")),
                .author = text(message, "/authorName"),
                .avatar = lastThumbnail(message, "/authorPhoto/thumbnails"),
                .amount = paid ? text(message, "/purchaseAmountText") : std::string(),
            };
            simdjson::dom::array runs;
            if (!message.at_pointer("/message/runs").get_array().get(runs)) {
                for (const auto run : runs) {
                    Element emoji;
                    if (run["emoji"].get(emoji)) {
                        entry.text += json::string(run, "/text");
                    } else if (json::boolean(emoji, "/isCustomEmoji")) {
                        entry.text += json::string(emoji, "/shortcuts/0");
                    } else {
                        entry.text += json::string(emoji, "/emojiId");
                    }
                }
            }
            if (!entry.text.empty() || paid) {
                page.messages.push_back(std::move(entry));
            }
        }
    }
    // The next token sits under one of several keys depending on the client.
    for (const auto key :
         {"timedContinuationData", "invalidationContinuationData", "reloadContinuationData"}) {
        Element next;
        if (!chat.at_pointer(std::string("/continuations/0/") + key).get(next)) {
            page.continuation = json::string(next, "/continuation");
            page.pollMs = static_cast<int>(json::integer(next, "/timeoutMs"));
            break;
        }
    }
    return page;
}

std::string_view visitorData(Element root) {
    return json::string(root, "/responseContext/visitorData");
}

std::string_view errorMessage(Element root) {
    return json::string(root, "/error/message");
}

} // namespace sloptube::innertube::parser
