# Contributing

## Style

- Format with the repository's `.clang-format` (LLVM base, 4-space indent, 100 columns, braces
  always inserted): `clang-format -i <files>`. `.editorconfig` covers whitespace for other files.
- C++23. No exceptions in the code base's own APIs (errors are `Result<T>`), no RTTI
  (`-fno-rtti`).
- Names: `PascalCase` types, `camelCase` functions and variables, `m_` members, `k` constants,
  `g_` for the rare global.
- Comments explain *why*, not what; keep them short.
- Java (`android/java`) only receives what Android delivers to Java classes alone and hands it
  to native code; logic stays in C++. It compiles with `-Xlint:all -Werror`.
- CI rejects unformatted code (see [building.md](building.md#continuous-integration)).
- Warnings are errors in review: the build uses `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`
  and must stay clean.

## Single source of truth

Never write a value twice. Versions, endpoints, sizes, colours, durations and strings each have
one home (see [architecture.md](architecture.md#rules)). If two files need the same value, one
of them exposes it.

## Performance

The target is a low-end phone (Helio G80). Prefer:

- work on the `TaskQueue` workers, never blocking the main thread on the network;
- render-on-demand (only set `frame.animating` while something moves);
- virtualised lists and bounded caches (thumbnails LRU, avatar queue);
- zero-copy media paths (hardware decoder → `AImageReader` → external texture).

## Testing a change

- Core/parsers: `sloptube-cli` against the live service (see [cli.md](cli.md)).
- App: `cmake --build build/android-release --target apk-run`, then compare with screenshots of
  the official app on the same phone.

Account actions (like, subscribe, comment, save to a playlist, create a playlist) act on a real account: test them on a throwaway
video or revert them right after.
