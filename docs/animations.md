# Animations

The official app feels alive because almost everything moves a little. SlopTube reproduces the
same motions with a handful of primitives in `src/app/ui/Motion.hpp` while keeping the
render-on-demand loop: a view that is animating sets `frame.animating`, which requests the next
frame; when everything settles the app goes back to sleep.

## Primitives

| Primitive | Purpose |
| --- | --- |
| `easeOutCubic`, `easeInOutCubic`, `easeOutBack` | Material-style curves; `easeOutBack` overshoots for "pop" feedback |
| `progress(now, start, duration)` | Normalised time of a fire-and-forget animation |
| `approach(value, target, seconds)` | Frame-rate independent exponential smoothing |
| `withAlpha(color, alpha)` | Fades a packed colour |
| `Layer` | Records the vertices and draw commands emitted between its construction and `end()`, then translates, scales about a pivot and fades them as a whole, clip rectangles included |

`Layer` is what makes whole-screen transitions cheap: screens draw normally and the layer moves
their geometry afterwards. No off-screen render target is needed.

Durations live next to the primitives: screens 280 ms, sheets 320 ms, fades 200 ms, pops 350 ms.

## Catalogue

| Animation | Where |
| --- | --- |
| Pushed screens slide in 20 % from the right while fading in | `App::drawScreen` |
| Watch page rises from below while fading in | `App::drawScreen` |
| Tab content cross-fades on switch | `App::drawTab` |
| Bottom sheets slide up, follow a downward drag on their header, and slide out when dismissed | `App::drawSheet` |
| Modal scrim fades with the account sheet | `App::drawSheet` |
| Touch ripple spreads from the finger, clipped to the pressed row or button | `drawPressed` |
| Buttons shrink slightly while pressed | `pillButton` |
| Icon buttons grow a circular highlight | `iconButton` |
| Like/dislike icon pops (overshoot) when selected | `App::drawWatchHeader` |
| Skeleton cards with a shimmering band while a feed loads | `drawSkeleton` |
| Thumbnails and avatars fade in when their texture arrives | `drawThumbnail`, `drawAvatar` |
| Toasts fade and lift in, then fade out | `App::drawToast` |
| The watch page follows a downward drag of its video, fading over the screen beneath, and springs back if released early | `App::pullWatch`, `App::drawScreen` |
| The video shrinks from its place on the watch page into the mini player | `App::drawMiniPlayer` |
| The mini player follows the finger and glides to the nearest corner | `App::drawMiniPlayer` |
| Kinetic scrolling with exponential friction | `Scroller` |
| Spinner while buffering | `Canvas::spinner` |
| Launch: the logo of Android's splash screen shrinks into the play glyph of a loading bar, which creeps up while the home feed loads, completes once it is in, and fades out. It lasts as long as the feed takes (at most 8 s), never a fixed delay | `App::drawSplash` (`SplashScreen.cpp`) |
| Seeking: the storyboard frame and time pill follow the finger above the bar, the handle grows to 20 dp | `App::drawScrubPreview` |
| Precise seeking: the film strip follows the finger under a fixed line | `App::drawFineSeek` |
| Double-tap like on a short: a white thumb pops and fades amid eight red and white rays shooting out (SlopTube's own design) | `App::drawLikeBurst` |
