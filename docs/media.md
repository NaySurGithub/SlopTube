# Media pipeline

Playback is native end to end: InnerTube gives stream URLs, `HttpSource` downloads them, the
NDK `AMediaExtractor`/`AMediaCodec` decode them in hardware, video frames land in an
`AImageReader` sampled by GL as an external texture, and AAudio plays the sound. The code is in
`src/core/innertube/Streams.*` (choosing streams) and `src/app/media/` (playing them).

## Getting streams

`Api::player()` asks the clients of `kPlayerOrder` (`ClientProfile.cpp`) in turn:

1. **VISIONOS**, the only client found whose adaptive streams download completely. It needs the
   `visitorData` of an earlier response. The app always loads a feed first, so it has one.
2. **ANDROID**, whose progressive itag 18 (360p, muxed) is complete.

Every other client tried has been dropped:

- ANDROID, ANDROID_VR and IOS serve adaptive URLs that answer 403 after about the first 60 s of
  media.
- SABR (`serverAbrStreamingUrl`) stops at the same point and then asks for a sign-in.
- The OAuth token is refused by every player client except TV, which serves no direct URLs.

Only formats with a direct `url` are used; ciphered formats are skipped. A broadcast in progress
(`videoDetails.isLive`) gets the same kind of URLs from VISIONOS, for segments rather than files
(see [Live streams](#live-streams)); ANDROID gives none for it.

## Choosing streams (`core/innertube/Streams.*`)

- `selectStreams(player, {maxLines})` returns one entry per quality label, best first:
  - **Video:** avc1 is preferred over VP9, and AV1 is skipped.
  - **Audio:** the default track first, non-DRC, AAC-LC before Opus.
  - **Fallback:** without a complete video + audio pair it falls back to the progressive format.
- `autoQuality(streams, player, {maxLines, bandwidthBps})` picks the best quality that fits
  60 % of the measured bandwidth. It caps at 720p until bandwidth has been measured, and never
  goes above the screen.

## Playing (`app/media/`)

| Piece | Role |
| --- | --- |
| `HttpSource` | Ranged HTTP reads in 1 MB blocks with about 30 s of read-ahead (2–16 blocks); feeds the extractor through `AMediaDataSource`; measures bandwidth (shared across videos) |
| `Player` | Closing a video hands its session to a reaper thread, which joins the decoder (codec, sources, audio: tens of milliseconds) away from the UI; the next video's decoder waits for it, since phones have few hardware decoders. Separate video and audio sources and decoders. Audio is the clock and video frames are released against it. Seeks apply to both. Each opened video is a `Session` (streams, state, decoder threads, its own `AImageReader`); a second, standby session can be preloaded |
| `VideoSurface` | `AImageReader` → `EGLImage` → `GL_TEXTURE_EXTERNAL_OES`, drawn by ImGui like any image (zero copy). Letterboxes the frame, optionally zoomed and cropped to the box (texture coordinates are cropped, nothing is drawn outside the box) |
| `LiveSource` | The segments of a live stream, fetched in order a few ahead of the one being demuxed (see [Live streams](#live-streams)) |
| `Log.hpp` | The single logcat tag |

### Quality

`Player::qualities()` lists what the video offers. Other calls:

- `selectQuality(index)` switches quality; `std::nullopt` means Auto.
- `currentQuality()` and `autoQuality()` report the state for the menu.

A switch works like this:

1. The new stream is opened on a helper thread at the current position.
2. The video decoder is swapped while audio keeps playing, so there is no audio gap. The picture
   holds briefly until the next keyframe.
3. A manual choice is remembered as a resolution for the following videos.
4. If the switch fails, playback stays on the current quality.

The watch page's settings button opens the menu (`App::drawPlayerMenu`). Rows show "Auto
(1080p60)" and then each quality.

### Speed

Pressing and holding on the video (watch page, fullscreen or Shorts) plays at 2x until the finger
lifts, with an "x 2 ▸▸" pill at the top, as in the official app (`App::holdForSpeed`):

- **Gesture:** the press must last 0.4 s without moving past the touch slop, so scrubbing and
  pulling the video down are never holds. The phone vibrates as 2x starts, with the system's
  long-press effect, the same call as the official app. The release is swallowed, so it does not
  also toggle the controls or press a button. It only starts while playing.
- **Easy seek:** on the watch page, dragging the finger after the hold has started seeks instead,
  as in the official app: a 25 ms pulse, back to 1x, then the same preview as the progress bar,
  sliding the full width moving through the whole video.
- **Audio:** `TimeStretch` changes tempo without changing pitch (WSOLA). Each 10 ms output hop
  takes the input segment within ±6 ms of where it belongs that best continues the previous one
  (normalised cross-correlation), and cross-fades the two. At 1x it takes exactly the next
  segment, so the output is bit-identical to the input and no search runs. It sits in the
  AAudio callback between the PCM ring and the device, and does not allocate there.
- **Clock:** the audio clock counts the media frames the played output stands for, not the
  device frames, so video frames (released against that clock, late ones dropped) and subtitles
  follow the speed with no extra work. A/V sync holds at 2x and after release.
- `Player::setSpeed` applies immediately; closing a video resets it to 1x.

Measured on the Galaxy A14: an 8 s hold advanced playback 17 s over 9.5 s of wall time
(7.6 s at 2x plus 1.9 s at 1x).

### Fullscreen

`App::setFullscreen` drives `Platform::setFullscreen`:

- **Entering** locks sensor-landscape and hides the system bars; a swipe shows them briefly. The
  video is pillarboxed to its aspect ratio.
- **Leaving** locks portrait and shows the bars. The lock is released once the accelerometer
  sees the phone upright, as in the official app.
- **Triggers:** the fullscreen button, or turning the phone to landscape while a video plays.
- **Back** leaves fullscreen first.

**Zoom.** The video starts fitted, with black bars at the sides as in the official app.
Pinching zooms it live (`App::drawZoomableVideo`, from `Touch::pinchScale`). On release:

- nearer the fitted size than the zoom that fills the screen → back to fitted;
- up to 15 % past the fill zoom → snaps to filling the screen (cropping top and bottom), with
  the "Zoomed to fill" pill for 2 s (the official app's wording);
- further → stays at that zoom, up to 8×. While pinching past the fill zoom, a circled factor
  ("2.7x") shows at the top, as in the official app.

The phone ticks when the pinch reaches the zoom that snaps to fill, and clicks when the video
settles fitted or filling, but not at a free zoom.

Leaving fullscreen resets the zoom. Zoom is always about the centre; a zoomed video cannot be
panned.

`main.cpp` re-reads the window insets for a second after a rotation, because Android reports
them late. The GL context and the playback survive rotation (`configChanges` in the manifest).

### Shorts

A short whose video is close to the page's shape (cropping at most 30 %, e.g. 9:16 on a 20:9
screen) fills the page, cropping its sides, as in the official app. Other shorts (16:9 videos
in the feed) are letterboxed.

Swiping is made instant by fetching ahead (`App::prefetchShorts`, `screens/ShortsPage.cpp`):

- the `next` (likes, channel, comments) and `player` responses of the two following shorts and
  the previous one, kept for at most 4 shorts;
- their posters, through `Thumbnails`;
- once the current short plays, the next one is opened on the player's **standby session**
  (`Player::preload`). It decodes its first frame and stays paused, with the minimum read-ahead
  (2 blocks per stream) so it does not compete with the short being watched. When the user
  swipes to it, `Player::promote` makes it current: the frame is already there and the audio
  starts at once. Only one standby exists; it is closed when leaving the Shorts tab or opening
  a regular video.

A short reached without a standby (swiping back, or two swipes in quick succession) still
starts from its prefetched `player` response, about a second after the swipe.

### Seeking

Three gestures seek, behaving as in the official app:

- **Progress bar:** a drag starting in the 32 dp band at the bottom of the player. The storyboard
  frame and the time follow the finger, with the chapter title above the time.
- **Easy seek:** a drag after holding the video for 2x (see [Speed](#speed)).
- **Precise seeking:** pulling the finger 45 dp up while dragging the bar pauses the video and opens
  a film strip, with a 25 ms pulse. Its square 72 dp frames are the storyboard cells, cropped to
  their middle, one per storyboard interval (2 s without a storyboard), under a 3 dp line. Dragging
  anywhere on the video moves the strip, and the release seeks there. Pulling 45 dp back down
  returns to the bar. The time, a 56 dp play button and a close button sit above; play resumes
  playback, close restores the state it found. "Pull up for precise seeking" shows under the
  pill until precise seeking has been used once.

**Seek undo** (`SeekUndo`): once the handle has gone 25 dp from where playback was, bringing it
back within 20 dp clicks and the pill reads "Release to cancel"; releasing in the next 2 s leaves
playback where it was. The Shorts bar has it too; its seeks are live, so it seeks back.

**Chapters** come from the `next` response (see [innertube.md](innertube.md#chapters)). The bar is
split at them with 2 dp gaps (3 dp while seeking), the time row ends with the current chapter's
title, and the phone ticks each time a seek crosses into another chapter.

### Live streams

A live format has no `contentLength` but a `targetDurationSec` (`Format::segmentSeconds`, 5 s):
its URL with `&sq=<n>` returns segment *n*, and without it the newest one. Every segment is a
complete fragmented MP4 file (`ftyp`, `moov`, then `emsg`, `moof`, `mdat`) whose sample times
continue the stream's timeline, and whose `emsg` text carries `Sequence-Number: <n>`. Measured:

| Request | Answer |
| --- | --- |
| No `sq` | The edge segment, at once |
| `sq` behind the edge | The segment, at once (the stream keeps `maxDvrDurationSec`, 12 h) |
| `sq` = edge + 1 | Held until the segment exists (up to 5 s), then sent |
| `sq` further ahead | 204 No Content after a few seconds |

`LiveSource` reads the edge's number, starts 3 segments behind it (about 15 s, so the next one is
always out before it is needed) and keeps 3 segments downloaded ahead. The player demuxes each
one with its own `AMediaExtractor` over the bytes in memory (`SegmentFile`), and moves the same
decoders on to the next segment when one is used up (`Input::advance`), so playback runs across
segments without a gap. Audio and video start at the same segment. A quality switch opens the
new video stream at the segment audio is playing, and the usual switch takes over from there.
The broadcast is over when segments stop coming (10 misses, 2 s apart), and playback ends.

**Rewinding.** The server keeps far more than `maxDvrDurationSec` (12 h; segments 139 h old
still downloaded), and the app offers those 12 h. Segment *n* starts at about
`offset + n * targetDurationSec` (within half a second: audio segments alternate 4.992 and
5.016 s), where the offset comes from the segment audio is playing and its first sample time.
The edge is the head learned by the last source started near it, moved on by one segment every
5 s. `Player::liveWindow` gives the playable range in the player's clock; a seek inside it starts
new sources at the segment holding the target and discards samples before it (`restartLive` in
the decoder loop), and a seek within three segments of the edge starts at the edge again.

The watch page's bar spans the window. "LIVE" with a red dot shows at the edge; behind it, the dot
turns grey with how far behind ("LIVE -12:34"), and tapping it goes back to live. The seek pill
shows the time behind live. Speed, 2x, precise seeking (there is no storyboard) and downloading
are not offered.

Closing a stream does not wait for a held request: `http::Request::cancelled` makes libcurl give
up within about a second.

### Haptics

`Platform::haptic` makes each vibration with the standard Android APIs:

| `Haptic` | How | When |
| --- | --- | --- |
| `LongPress` | `View.performHapticFeedback(LONG_PRESS)` | 2x starts |
| `ContextClick` | `View.performHapticFeedback(CONTEXT_CLICK)` | Double-tap like on a short |
| `Click` | `VibrationEffect.EFFECT_CLICK` | Zoom settles fitted or filling; seek undo arms |
| `Tick` | `VibrationEffect.EFFECT_TICK` | Pinch reaches the fill zoom; a seek crosses a chapter |
| `Seek` | 25 ms one-shot at full amplitude | Precise seeking opens; easy seek starts |

The first two follow the phone's touch feedback setting; the others use the vibrator, which needs
the `VIBRATE` permission (granted at install).

### Speed

The player menu (the gear) lists Quality and Playback speed: 0.25x to 2x, as in the official app,
kept for the following videos until the app closes. Holding for 2x returns to it on release.
`TimeStretch` handles every speed the same way.

### Background and picture-in-picture

These go through the Java bridge (see [architecture.md](architecture.md#java-bridge)):

- **Background playback.** Leaving the app destroys the window but not playback (unless turned
  off in Settings, then `APP_CMD_STOP` pauses). The decoder goes on with frames released
  unrendered (`Player::setVideoHidden`), so audio and video stay in sync and the decoder never
  stalls on a full image queue. `PlaybackService`, a `mediaPlayback` foreground service, keeps
  the process alive.
- **Media session.** `App::syncPlayback` publishes the video (title, channel, duration, position,
  state, speed, whether previous and next exist) when it changes, and the position only when it
  drifts by more than 2 s from what the session extrapolates. The service shows the media
  notification (with the thumbnail) and turns its commands, headset and Bluetooth buttons, the
  lock screen's and the PiP window's into `MediaCommand`s. It requests audio focus while playing:
  a call pauses and resumes after it, another app playing pauses for good, unplugging headphones
  pauses.
- **Picture-in-picture.** While a watch-page or mini-player video plays, the activity allows it
  with the video's aspect ratio: Android 12+ enters by itself when the user goes home, Android 11
  on `onUserLeaveHint`. In the window only the video is drawn. Closing the window pauses.
- **Autoplay.** When a video ends, the next one (`nextVideo`: the list it came from, else the
  first related video) plays after a 5 s countdown with Cancel and Play now, at once when the app
  is not on screen. The event loop wakes twice a second while playing unseen to notice the end.

## Downloads and offline viewing (`app/downloads/`)

"Download" on the watch page opens a sheet listing the qualities of `selectStreams` with their
sizes. `Downloads` then saves the video under `<data>/downloads/<video id>/`:

| File | Content |
| --- | --- |
| `manifest.json` | Item (title, channel, …), chosen video/audio formats (itags, sizes), caption tracks, `complete` |
| `video`, `audio` | The streams; `.part` while incomplete |
| `thumb`, `avatar` | Images shown in lists while offline |
| `captions/<n>` | json3 cues of each track (up to 12, written and auto-generated) |

- One download runs at a time, on its own thread, in 4 MB ranged chunks (`http::kRangeParameter`).
  Progress wakes the UI through the task queue.
- A download interrupted by closing the app resumes from its `.part` files at the next launch.
  Stream URLs expire, so a resumed download asks `player` again and keeps the same itags.
- **Playback.** `http::Client` serves `file:///path?…&range=a-b` from disk. A finished download
  is handed to the normal `Player` as an `innertube::Player` with `file://` URLs
  (`Downloads::localPlayer`), so the same `HttpSource`, decoders and quality code play it.
  `HttpSource` does not count local reads in the bandwidth estimate.
- **Subtitles.** `Subtitles::openLocal` takes the saved tracks (`Downloads::localCaptions`); the
  usual cue fetch reads them through `file://`. Machine translation is not offered offline.
- Downloaded videos play from disk even when online.
- **Offline.** A feed that fails for lack of a connection (`http::kTransportError`) shows "You're
  offline" with "Go to downloads". The Downloads screen is also in "You".

## Known limitations

- VISIONOS is undocumented and could be closed at any time. Without it, playback falls back to
  360p, and live streams do not play at all.
- Live streams start about 15 s behind the edge. Upcoming streams and premieres show the
  server's reason instead.
- Background playback keeps decoding video it does not show, which costs some battery.
- Auto picks a quality when a video opens and when Auto is re-selected. It does not adapt in the
  middle of a video.
- Qualities above 1080p (VP9/AV1 only) are not offered.
- After a quality switch combined with a seek, the picture can hold for a few seconds while
  frames are decoded up to the target.
- At 2x the decoder has to decode twice as many frames. High frame rates at 1080p can exceed
  what the A14's decoder manages; late frames are then dropped, while audio stays smooth.
