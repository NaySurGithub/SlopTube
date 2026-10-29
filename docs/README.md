# SlopTube documentation

SlopTube is a lightweight YouTube client for Android written entirely in C++ (no Java or
Kotlin). It talks to YouTube through InnerTube, the private API the official apps use, signs in
with a real Google account, and draws a YouTube-like interface with Dear ImGui on OpenGL ES.

| Document | Contents |
| --- | --- |
| [Building](building.md) | Toolchain, presets, packaging the APK and its icon, installing on a device, CI |
| [Releasing](releasing.md) | Cutting a release, the release signing key, signing locally |
| [Website](site.md) | The GitHub Pages site: what it is built from, building it locally |
| [Architecture](architecture.md) | Source layout, threading model, frame loop, ownership rules |
| [InnerTube API](innertube.md) | Endpoints, client profiles, parsed renderers, account actions |
| [Authentication](authentication.md) | Google device sign-in, token refresh, multiple accounts |
| [Media pipeline](media.md) | Stream selection, hardware decoding, audio clock, quality, fullscreen |
| [User interface](ui.md) | Canvas, views, screens, sheets, touch handling |
| [Animations](animations.md) | Motion primitives and where each animation lives |
| [Text rendering](text.md) | Fonts, Arabic shaping, bidirectional text |
| [Localisation](i18n.md) | Adding and translating user-facing strings |
| [Command-line tool](cli.md) | `sloptube-cli`, the host-side driver for the core library |
| [Contributing](contributing.md) | Code style, formatting, conventions |
