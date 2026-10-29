<p align="center">
	<img src="android/logo.svg" alt="SlopTube" width="120">
	<br>
	<b>SlopTube</b>
	<br>
	Lightweight YouTube client for Android written in C++23
</p>

<p align="center">
	<img src="https://img.shields.io/badge/language-C%2B%2B23-00599C" alt="C++23">
	<img src="https://img.shields.io/badge/platform-Android%2011%2B%20arm64-lightgrey" alt="Platform">
	<img src="https://img.shields.io/badge/UI-Dear%20ImGui-blue" alt="Dear ImGui">
	<br>
	<a href="https://github.com/NaySurGithub/SlopTube/actions/workflows/ci.yml"><img src="https://github.com/NaySurGithub/SlopTube/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
	<a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-green" alt="License"></a>
</p>

<table>
	<tr>
		<td><img src="docs/screenshots/search.jpg" alt="Searching for MrBeast: the channel, then its videos"></td>
		<td><img src="docs/screenshots/watch.jpg" alt="Watching a MrBeast video with its channel, actions and comments"></td>
		<td><img src="docs/screenshots/channel.jpg" alt="MrBeast's channel page with its shelves"></td>
	</tr>
	<tr>
		<td align="center">Search</td>
		<td align="center">Watch page</td>
		<td align="center">Channel</td>
	</tr>
</table>

## What is this?

SlopTube is a native YouTube client for Android. There is no Java, no Kotlin and no Gradle: the
whole app is a `NativeActivity` in C++. It talks to YouTube through InnerTube, the API behind the
official apps, signs in with a real Google account and draws an interface close to the official
app's with Dear ImGui on OpenGL ES. It is built to stay smooth on a low-end phone (Helio G80):
nothing is drawn while nothing moves, and video goes from the hardware decoder to the screen
without a copy.

- **Feeds** - Home with Shorts shelves and community posts, Subscriptions, history, playlists,
  search and channels
- **Account** - Google sign-in with a device code, several accounts, likes, subscriptions,
  comments and replies, saving to playlists
- **Player** - hardware decoding up to 1080p60, quality choice, 2x speed while holding, seek
  previews, chapters, precise seeking, seek undo, fullscreen with pinch to zoom, mini player, and
  the official app's haptic feedback
- **Live** - broadcasts in progress, at every quality, about 15 s behind the edge
- **Shorts** - vertical pager with the next short preloaded, so swiping is instant, and double
  tap to like
- **Subtitles** - every track, with machine translation
- **Downloads** - videos with their subtitles, watchable offline
- **Text** - every script, Arabic shaping, right-to-left text and colour emoji
- **Languages** - English and French, following the phone

## Downloads

Signed APKs are published on the [releases page](https://github.com/NaySurGithub/SlopTube/releases).
Every push to `main` also builds a debug-signed APK, available from the latest
[CI run](https://github.com/NaySurGithub/SlopTube/actions/workflows/ci.yml). Both need Android 11
or newer on a 64-bit ARM phone. The two are signed with different keys, so switching from one to
the other needs an uninstall first.

## Building

Requires CMake 3.28+, Ninja, the Android SDK (platform 35, build-tools 35.0.0), the NDK r27 or
newer and JDK 17. The first configure needs network access to fetch the dependencies.

```
cmake --preset android-release
cmake --build --preset android-release
cmake --build build/android-release --target apk-install
```

`sloptube-cli`, a desktop driver for the same core library, builds with the `host-debug` preset
and needs libcurl. The [documentation](docs/README.md) covers the toolchain, the architecture,
the InnerTube endpoints, the media pipeline and the interface.

## Contributing

Pull requests are welcome. Read [docs/contributing.md](docs/contributing.md) first: code is
formatted with the repository's `.clang-format`, and CI rejects unformatted code.

## Licensing information

SlopTube is licensed under the [GNU General Public License v3.0](LICENSE) or any later version.
Third-party components and their licences are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

SlopTube uses YouTube's private InnerTube API, which can change or stop working at any time.
Downloading videos may be against YouTube's Terms of Service; you are responsible for how you use
the app and for respecting the rights of content creators. The screenshots show publicly
available videos for illustration only.

NOT AN OFFICIAL YOUTUBE PRODUCT. NOT APPROVED BY OR ASSOCIATED WITH GOOGLE LLC OR YOUTUBE.
All brands and trademarks belong to their respective owners.
