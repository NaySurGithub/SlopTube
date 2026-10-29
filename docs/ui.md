# User interface

The interface is drawn every frame with Dear ImGui's draw lists, but almost none of ImGui's
widgets are used: layout, hit testing and scrolling are SlopTube's own so the result matches the
official app pixel for pixel. Reference screenshots of the official app were taken on the target
phone and used to sample colours and measure spacing (`ui/Theme.hpp`).

## Building blocks (`src/app/ui`)

| File | Role |
| --- | --- |
| `Theme.hpp` | Colours, sizes in dp, text sizes in sp and weights per role (see [text.md](text.md#typography)), timing constants |
| `Canvas` | Density-aware drawing: text (any script, four weights, Android sizes and line height), paragraphs with ellipsis, icons drawn from vector paths, avatars, rectangles |
| `Touch` | Recogniser for taps, drags, fling velocity, press duration and overlay blocking (first pointer), and pinches (first two pointers) |
| `Scroller` | Kinetic scrolling on one axis with exponential friction; claims a drag only if it moves mostly along its axis, so shelves and lists nest |
| `Thumbnails` | Remote images: download and decode on workers (`AImageDecoder`), upload on the main thread, LRU of GL textures, cancellation of rows flung past |
| `Avatars` | Uploader avatars for feeds that carry none. Items are keyed by channel id, or by video id when the feed has no channel (the TV home feed). Lookups run lazily, three in flight, newest first, and are cached on disk in `avatars.txt` |
| `VectorIcons` | Navigation bar icons. Each one is a signed distance field (boxes, circles, triangles) rasterised once at its exact pixel size into one texture. They are SlopTube's own generic shapes: YouTube's logos and Shorts icon are trademarks and are not reproduced |
| `Views` | Reusable pieces: virtualised feed lists (card and row styles), shelves, thumbnails with crop and fade, avatars, buttons, ripples, skeletons, status messages |
| `Motion` | Easing curves and layers (see [animations.md](animations.md)) |
| `Logo` | The logo drawn with ImGui from the shape generated out of `android/logo.svg`, with the gradient applied by vertex shading; used by the launch animation |
| `Strings` | Localised text (see [i18n.md](i18n.md)) |
| `SeekUndo` | The official app's seek undo: when a seek brought back near where playback was is cancelled |

All sizes are given in dp/sp and converted with the display density (`Canvas::dp`).

## Screens

| Screen | Code | Content |
| --- | --- | --- |
| Home, Subscriptions | `App::drawTab` | Card feed under the top bar (wordmark, search, account avatar). Home interleaves Shorts shelves (after the first video, then every 12 videos) and post cards (after the 4th video, then every 6 videos) |
| Post card | `ui/PostCard.cpp` | Avatar, author and date (tap: channel), text clamped to 3 lines with "Read more" expanding it in place, then one rounded image, a sideways carousel of square pages with an "n/total" counter, poll answers in outlined rows with the vote total, or an attached video (tap: watch page); like/dislike with the like count, comments icon with the count. Any other tap opens the post |
| Post | `PostPage.cpp` | The whole post above its comments (same list as the comments sheet, read-only), "N replies" opening the replies |
| Shorts shelf | `ShortsPage.cpp` (`drawShortsShelf`) | Shorts icon and title, then a 2 × 3 grid of 2:3 tiles with the title over a scrim, as measured on the official app; a tap opens the Shorts tab at that short |
| Shorts | `ShortsPage.cpp` (`drawShorts`) | Full-screen vertical pager: video, a right-hand column (like, dislike, comments, save, share; 60 dp apart, 28 dp icons with shadowed labels, as measured), channel and Subscribe, then the title on the bottom margin whatever its length (a tap opens the description sheet), progress bar; tap to pause (once no second tap can follow, 0.3 s); a double tap likes the short with a white thumb popping in a burst of rays (`drawLikeBurst`); hold for 2x. Dragging along the bottom seeks live (every 0.4 s, only when the position changed) with everything else hidden and a red bar with its handle, as in the official app, which has no storyboard there; it can be undone like the watch page's |
| You | `App::drawYou`, `AccountPage.cpp` | Profile header with "Switch account", history shelf, "Downloads", playlists |
| Search | `SearchPage.cpp` | Search field, card results (videos, playlists). A channel gets its own card, as on the official app: 72 dp avatar, name, handle and subscribers, and "Go to channel" (`drawChannelCard`). While the field has focus: past searches matching the text (clock icon, the last 30 kept in `searches.txt`), then suggestions; the arrow puts one in the field. With results, the filter button opens chips grouped as in the official filter menu (prioritize, upload date, type, duration, features), lit while a filter is on |
| Settings | `SettingsPage.cpp` | From the gear in the home bar: text and button size (Normal, Large 1.15x, Extra large 1.3x, multiplied by Android's own text size, up to 1.45x: the whole interface is drawn bigger, touch areas included, so layouts stay whole), background play, picture-in-picture, autoplay and upload notifications (`settings.json`), clearing the search history, and in release builds the automatic update check and "Check for updates" |
| Update | `UpdatePage.cpp` | A newer release: its version and notes, "Update now" or "Later", the download's progress, then Android's install confirmation (see [releasing.md](releasing.md#updates-in-the-app)) |
| Watch | `App::drawWatch`, `WatchPage.cpp` | The gear opens Quality and Playback speed. Player, title, views/date ("…more" opens the description), channel row with Subscribe, a sideways-scrolling action row (like/dislike pill, Share, Download with its progress, Save), comments teaser, related videos. Dragging along the progress bar (32 dp band, even with the controls hidden) shows the storyboard frame under the finger, dimmed, with a time pill 36 dp above the bar and the chapter title above it; the red fill stays at the playback position and the 20 dp handle follows the finger, as measured on the official app. The bar is split at chapters. Pulling up opens precise seeking, dragging after a 2x hold seeks, and a seek brought back to where playback was is cancelled (see [media.md](media.md#seeking)). A live stream shows a red dot and "LIVE" instead of the time, with no bar and no Download (see [media.md](media.md#live-streams)) |
| Comments sheet | `WatchPage.cpp` | Top/Newest chips, comment field, comments with like/dislike/reply, "N replies" opening the replies view with its own field |
| Description sheet | `WatchPage.cpp` | Title, likes/views/date figures, full description, channel row |
| Channel | `ChannelPage.cpp` | Banner, large avatar, name, handle and subscribers, Subscribe, then tab chips (Home, Videos, Shorts, Live, Playlists) scrolling sideways: Home shows every shelf of the channel's home tab, the others their list (loaded when first opened), scrolling with the header |
| Live chat | `LivePage.cpp` | In place of the comments teaser on a live stream: the newest message; a tap opens the sheet, newest at the bottom, following new messages until scrolled up. Polled while the watch page or mini player is shown |
| Autoplay | `Background.cpp` (`drawAutoplay`) | Over the ended video: "Up next in 5", the next title, Cancel and Play now |
| Accounts sheet | `AccountPage.cpp` | Signed-in accounts with the active one checked, "Add account", "Sign out" |
| Navigation bar | `NavigationBar.cpp` | Five equal slots: Home, Shorts, a round "create" button, Subscriptions, You (the account photo, ringed when selected). Icons are outlined, and filled when selected; all labels are white. Measured on the official app: 48 dp bar, 1 dp divider, icons 16 dp and labels (12 sp) 36 dp below its top, 36 dp create button |
| Mini player | `MiniPlayer.cpp` | Dragging the watch page's video down, Back, or switching tabs shrinks the video into a floating window (half the screen width) that keeps playing. It can be dragged and snaps to the nearest corner, in the direction of a fling. A tap shows its controls (close, play/pause, reopen); a double tap reopens the watch page |
| Save sheet | `SavePage.cpp` | The account's playlists with a checkbox each and their privacy; a tap adds or removes the video (optimistic, reverted with a toast on failure); "New playlist" opens a title field with privacy chips |
| Share | `SavePage.cpp` (`shareVideo`) | System share sheet with `https://youtu.be/<id>` (or `/shorts/<id>`) |
| Download sheet | `DownloadPage.cpp` | Qualities with their sizes, or the download's status with "Delete from downloads" |
| Downloads | `DownloadPage.cpp` | In "You": downloaded and downloading videos with quality, size or progress; a tap plays from disk (a failed one retries) |
| Offline state | `DownloadPage.cpp` (`drawOffline`) | Replaces a feed that failed for lack of network: "You're offline", "Go to downloads", "Retry" |
| Sign-in | `App::drawSignIn` | Device code and verification link |
| Launch | `SplashScreen.cpp` | Over everything, touch blocked, until the home feed is in (see [animations.md](animations.md)) |
| Playlist, History | `App::drawFeedScreen` | Row-style lists |

## Errors

Failures are told in plain words with a way out: the app's own technical messages ("http: ...",
"player: ...") become "Something went wrong" or "No internet connection" (`friendlyError`), while
the server's explanations, already in the user's language, are shown as they are. A video that
cannot play says so over the player with a Retry button.

## Input rules

- A tap is consumed by the first view that claims it, so controls drawn on top must be asked
  first. Overlays (sheets) are drawn into a higher ImDrawList channel *before* the screen under
  them, then `Touch::setBlocked(true)` hides the rest of the gesture from that screen. Code that
  blocks touch for a part of the screen restores the previous state (`Touch::blocked()`) rather
  than unblocking, so blocks nest (a sheet inside a screen under the mini player).
- **Multi-touch.** `Touch` follows pointers by id (`AMOTION_EVENT_ACTION_POINTER_DOWN/UP`). When a
  second pointer lands, the gesture becomes a pinch until every finger is up: the tap, drag,
  press or fling in progress is cancelled and none starts, so a pinch never pauses, scrubs or
  holds for 2x. `pinchScale()` gives the live distance ratio and `pinchEnded()` the final one, on
  the frame the second finger lifts.
- Rows inside a clipped list clamp their hit boxes to the list's clip rectangle so headers
  above them keep their taps.
- Text entry uses ImGui's `InputText`; the soft keyboard is shown whenever ImGui wants text
  input (`Platform::setKeyboardVisible`). Comment fields sit at the top of their sheet so the
  keyboard never covers them.

## Lists

`drawList` virtualises its items: each kind has a fixed height (a channel card is shorter than a
video card), only rows intersecting the viewport are drawn, and
the next page is requested when the viewport is within two screens of the end. It also takes
`ListInsert`s, full-width blocks placed after a given number of items (the Shorts shelves, the post cards)
that draw and handle taps themselves. Post cards have variable heights: `postCardHeight` lays
them out with `Canvas::lineCount` (wrapping without drawing) and caches the result per width and
clamp state, so the height used for scrolling always matches what is drawn. Comment lists
have variable heights; each row's height is measured when drawn and cached, and rows not drawn
yet use an estimate.
