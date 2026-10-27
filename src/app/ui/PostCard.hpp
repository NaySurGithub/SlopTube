#pragma once

#include "app/ui/Scroller.hpp"
#include "app/ui/Views.hpp"
#include "core/innertube/Types.hpp"

#include <cstdint>

namespace sloptube::app {

// A community post and its view state, as shown in the home feed and on its own page.
struct PostCard {
    innertube::Post post;
    bool expanded = false; // Whole text instead of the first lines ("Read more").
    Scroller carousel{Axis::Horizontal};
    // Layout cache: the height only depends on the width and on whether the text is clamped.
    float layoutWidth = 0;
    bool layoutClamped = false;
    float height = 0;
};

enum class PostLayout : std::uint8_t {
    Feed, // Text clamped to a few lines until expanded; a tap opens the post.
    Page, // The post's own page: whole text.
};

// What a tap on the card asks for; the caller acts on it (navigation goes through App::later).
enum class PostAction : std::uint8_t {
    None,
    Open,
    OpenChannel,
    OpenVideo,
    Rate,
};

// Height of the card at `width`; cached in `card`, so it is cheap to ask every frame.
[[nodiscard]] float postCardHeight(const Canvas& canvas, PostCard& card, float width,
                                   PostLayout layout);

// Draws the card with its top-left corner at `origin` and returns the tapped action. Taps are
// clipped to the current clip rectangle, so the card can sit inside a scrolling list.
PostAction drawPostCard(Frame& frame, PostCard& card, ImVec2 origin, float width,
                        PostLayout layout);

} // namespace sloptube::app
