# Building

## Requirements

| Tool | Version | Notes |
| --- | --- | --- |
| CMake | 3.28+ | Presets in `CMakePresets.json` |
| Ninja | any | Generator used by every preset |
| Android SDK | platform 35, build-tools 35.0.0 | `aapt2`, `zipalign`, `apksigner` |
| Android NDK | r27 or newer | The newest installed NDK is picked automatically |
| JDK | 17 | `javac` and `jar` for the Java bridge, `apksigner`, `keytool` |
| libcurl | system | Host builds only (the CLI) |

The SDK is located through `ANDROID_HOME`, `ANDROID_SDK_ROOT`, the Homebrew
`android-commandlinetools` prefix or `~/Library/Android/sdk`, in that order
(`cmake/AndroidToolchain.cmake`). There is no Gradle: the APK is assembled by CMake.

On macOS with Homebrew, put the JDK on the path before building:

```sh
export PATH="/opt/homebrew/opt/openjdk@17/bin:$PATH"
```

## Presets

| Preset | Output | Use |
| --- | --- | --- |
| `host-debug` | `build/host-debug/tools/cli/sloptube-cli` | Exercise the core library against the live service |
| `android-debug` | `build/android-debug/SlopTube.apk` | Debuggable app |
| `android-release` | `build/android-release/SlopTube.apk` | Optimised app (LTO, stripped, gc-sections) |

```sh
cmake --preset android-release
cmake --build --preset android-release        # builds and packages the APK
cmake --build build/android-release --target apk-install
cmake --build build/android-release --target apk-run
```

`apk-install` runs `adb install -r`; `apk-run` also launches the activity. A debug keystore is
created next to the build on first use if `~/.android/debug.keystore` is missing.

## Dependencies

All third-party code is fetched with `FetchContent` and pinned in one place,
`cmake/Dependencies.cmake`:

- **simdjson**: parses InnerTube responses.
- **curl + mbedTLS** (Android): HTTP transport. mbedTLS is built with its threading layer
  (`cmake/MbedtlsConfig.h`) because thumbnails, avatars and metadata are fetched concurrently.
- **Dear ImGui**: immediate-mode drawing and the text input widget.
- **FreeType** (Android): glyph rasterisation through `imgui_freetype`, including the named
  instances of variable fonts. Built without zlib, bzip2, PNG, HarfBuzz and Brotli.

Sources are collected with `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`, so new files are picked
up by re-running the build; no CMake list needs editing.

## Packaging

`cmake/AndroidPackaging.cmake` turns the shared library into an APK:

1. `aapt2 compile` builds the resources and `aapt2 link` links them with
   `android/AndroidManifest.xml.in` (`SlopTubeActivity`, a `NativeActivity`, and the playback
   service and upload job) against `android.jar`.
2. The Java bridge (`android/java/*.java.in`, configured with the application id as package) is
   compiled by `javac --release 11 -Werror` against `android.jar`, packed with `jar` and dexed by
   `d8` into `classes.dex`.
3. The library is stored uncompressed (`zip -0`) so it can be mapped directly
   (`extractNativeLibs="false"`); `classes.dex` is added next to it.
4. `zipalign -P 16` aligns it for 16 KB page devices.
5. `apksigner` signs with the debug keystore, or the release key for releases (see
   [releasing.md](releasing.md)).

### Icon

`android/logo.svg` is the only source of the logo. `cmake/Logo.cmake` reads its paths, stroke and
gradient, and fills:

- the templates of `android/res/` (an adaptive launcher icon with a monochrome layer for themed
  icons, and the theme whose background Android's splash screen uses; the colour comes from
  `kBackground` in `Theme.hpp`);
- `src/app/ui/LogoShape.hpp.in`, the shape drawn by the launch animation.

The SVG must keep its simple layout: one `id` per path (`play`, `drips`), absolute `M`/`L`/`C`/`Z`
commands only, one stroke width and two gradient stops. Configuration fails otherwise.

The version code is derived from the project version: `MAJOR * 10000 + MINOR * 100 + PATCH`.

## Continuous integration

`.github/workflows/ci.yml` runs on every push to `main`, on `v*` tags and on pull requests:

| Job | Runner | Checks |
| --- | --- | --- |
| Format | Ubuntu | `clang-format --dry-run --Werror` on `src/` and `tools/`, clang-format pinned in the workflow (`CLANG_FORMAT_VERSION`) |
| CLI | macOS | `host-debug` preset |
| Android APK | Ubuntu | `android-release` preset with NDK r27 and build-tools 35; the APK is uploaded as an artifact; on a `v*` tag it is signed with the release key |
| Release | Ubuntu | `v*` tags only: publishes the release-signed APK (see [releasing.md](releasing.md)) |

Fetched dependencies are cached per hash of `cmake/Dependencies.cmake`.
