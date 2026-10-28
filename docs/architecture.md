# Architecture

## Layout

```
src/
  core/                 Platform-independent library (also used by the CLI)
    auth/               Google device sign-in and stored accounts
    http/               libcurl wrapper with a shared connection/TLS cache
    innertube/          InnerTube requests, client profiles, response parsers
    json/               simdjson helpers and a tiny JSON writer
    util/               Result<T> (std::expected) and a protobuf encoder
  app/                  The Android application (NativeActivity)
    main.cpp            android_main: event loop and lifecycle
    App.{hpp,cpp}       Navigation, data loading, frame loop, tab and list screens
    screens/            Watch details, comments, channel, accounts, Shorts, mini player,
                        navigation bar, overlays
    downloads/          Offline copies of videos (streams, subtitles, images)
    media/              HTTP data source, decoder/player, video texture
    platform/           JNI services (keyboard, insets, locale, share, emoji) and EGL display
    ui/                 Canvas, views, touch, scrolling, images, motion, strings, theme
    ui/text/            Arabic shaping, bidirectional reordering, Unicode tables
    util/               TaskQueue (worker pool + main-thread mailbox)
tools/cli/              sloptube-cli, a host driver for core/
cmake/                  Toolchain, warnings, dependencies, APK packaging
android/                Manifest and resource templates, logo.svg (the logo's only source)
docs/                   This documentation
.github/workflows/       CI: formatting, CLI build, APK build
```

`core` never depends on Android or on the UI. Everything that touches the framework lives in
`app/platform`, and the UI only sees the client-agnostic model in `core/innertube/Types.hpp`.

## Threads

| Thread | Work |
| --- | --- |
| Main (`android_main`) | Input, UI state, drawing, GL. Owns every UI object. |
| Workers (`TaskQueue`, 4) | Network requests, JSON parsing, image decoding |
| Player | Demuxing and decoding (`media::Player`) |
| HTTP prefetch | Read-ahead for the media data source, or the segment fetcher of a live stream |
| Downloads | One video download at a time (`Downloads`) |
| AAudio callback | Pulls decoded PCM from a lock-free ring |

Workers never touch UI state. A worker finishes by posting a closure to the main thread
(`TaskQueue::post`), which wakes the looper; the closure runs at the start of the next frame.
Closures capture `std::weak_ptr`s to the objects they update (feeds, comment lists, channel
pages) so results for screens the user already left are dropped safely.

`TaskQueue` is destroyed before everything it serves, joining the workers before anything they
reference goes away. Only `Downloads` comes after it, so the download thread, which posts progress
to the queue, stops first.

## Frame loop

Rendering is on demand. `main.cpp` blocks in `ALooper_pollOnce(-1)` until an input event, a
posted task or a decoded video frame wakes it. `App::frame()` returns whether another frame
must follow immediately — while something animates, a list flings, a finger is down or the
keyboard is open. An idle screen costs no CPU or GPU time.

Navigation is never applied mid-draw: views call `App::later()` (a main-thread post) so the
screen being drawn is not destroyed under its own feet.

## Navigation model

- Four tabs (Home, Shorts, Subscriptions, You), each with its own feed. Shorts are also shown
  as shelves inside Home, and both use the same feed.
- A stack of `Screen`s on top (search, watch, playlist, history, channel, sign-in). Back pops
  the stack, then returns to Home, then leaves the app.
- At most one `Sheet` (comments, description, accounts, save, download) slides over the current
  screen; Back closes it first. Accounts, save and download sheets are modal (`SheetState::modal`).
- Leaving the watch page (Back, a drag down, another tab) moves it out of the stack into the
  mini player (`MiniPlayerState::watch`), which plays over every screen. Reopening pushes it back
  unchanged, and opening another video closes it.

## Data flow

```
View tap ──later()──▶ App::openX() ──TaskQueue::run──▶ Api::x() ──http──▶ InnerTube
                                                       │
                     UI state ◀──TaskQueue::post────── parser::x()
```

`Feed` holds a page of `Item`s plus its pagination token and the request that produced it (for
"Retry"). Lists ask for the next page when the user scrolls within two screens of the end.

## Rules

- **Single source of truth.** No constant or value is written twice: client versions live in
  `ClientProfile.cpp`, dependency versions in `cmake/Dependencies.cmake`, colours and sizes in
  `ui/Theme.hpp`, user-facing text in `ui/Strings.cpp`.
- **No exceptions, no RTTI.** Errors travel as `Result<T>` (`std::expected<T, Error>`).
- **Optimistic actions.** Likes, subscriptions and comments update the UI immediately and are
  reverted, with a toast, when the request fails.
