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
    platform/           JNI services (keyboard, insets, locale, share, emoji), the Java bridge's
                        native half (Bridge) and the EGL display
    notifications/      The check for new uploads run by the background job
    ui/                 Canvas, views, touch, scrolling, images, motion, strings, theme
    ui/text/            Arabic shaping, bidirectional reordering, Unicode tables
    util/               TaskQueue (worker pool + main-thread mailbox)
tools/cli/              sloptube-cli, a host driver for core/
cmake/                  Toolchain, warnings, dependencies, APK packaging
android/                Manifest and resource templates, logo.svg (the logo's only source)
  java/                 The Java bridge (templates: the package is the application id)
docs/                   This documentation
.github/workflows/       CI: formatting, CLI build, APK build
```

`core` never depends on Android or on the UI.

## Java bridge

Android delivers some events only to Java classes, so a small Java layer (`android/java`,
compiled by CMake with `javac` and `d8`, no Gradle) receives them and hands them to native code:

| Class | Role |
| --- | --- |
| `SlopTubeActivity` | Extends `NativeActivity`. Loads the library first, so `JNI_OnLoad` runs with the app's class loader; forwards notification taps (`onNewIntent`) and picture-in-picture changes; native code calls its methods to publish playback, allow picture-in-picture and schedule notifications |
| `PlaybackService` | Foreground service of type `mediaPlayback`: keeps the process alive while playing in the background, owns the `MediaSession` (notification, lock screen, headset, Bluetooth, the PiP window's controls), audio focus and pausing when headphones are unplugged |
| `UploadsJob` | `JobService` run about hourly while notifications are on; asks native code for new uploads and notifies them |

Java calls native code through natives registered in `JNI_OnLoad` (`platform/Bridge.cpp`). Their
events arrive on Java threads and are posted to the main thread like any worker result; events
sent before the app starts (a notification tap that launched it) wait for it. Native code calls
Java only on the main thread, through methods of the activity (`Platform`). Everything that touches the framework lives in
`app/platform`, and the UI only sees the client-agnostic model in `core/innertube/Types.hpp`.

## Threads

| Thread | Work |
| --- | --- |
| Main (`android_main`) | Input, UI state, drawing, GL. Owns every UI object. |
| Workers (`TaskQueue`, 4) | Network requests, JSON parsing, image decoding |
| Player | Demuxing and decoding (`media::Player`) |
| HTTP prefetch | Read-ahead for the media data source, or the segment fetcher of a live stream |
| Downloads | One video download at a time (`Downloads`) |
| Live segments | One per live stream being played (`LiveSource`) |
| Java main / binder threads | Bridge events (media buttons, PiP), posted to the main thread |
| Upload check | The background job's thread; uses its own HTTP client and session |
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
keyboard is open. An idle screen costs no CPU or GPU time. Without a window (playing in the
background), the loop wakes twice a second only to notice the end of the video for autoplay
(`App::idleWakeMs`).

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
