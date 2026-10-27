# Text rendering

YouTube titles, channel names and comments arrive in every script, often mixed in one string
(an Arabic title with Latin words and digits, a Korean title with emoji). Dear ImGui draws one
glyph per code point, left to right, so SlopTube adds font fallback, Arabic shaping and
bidirectional reordering on top of it. Glyphs are rasterised by FreeType (`imgui_freetype`). The code is in `src/app/ui/Fonts.*` and
`src/app/ui/text/`.

## Fonts (`ui/Fonts.cpp`)

`loadFonts()` builds one face per `Weight` (Regular, Medium, SemiBold, Bold: Roboto's 400, 500,
600 and 700, the weights the official app uses), each with the same chain of fallbacks merged in:

1. Roboto (the system UI font), then colour emoji (below), then the NotoSansSymbols files;
2. Arabic (`NotoNaskhArabicUI`), Hebrew, CJK, Korean;
3. one font for every other script found in `/system/fonts` (Devanagari, Thai, …).

- **Files are memory-mapped, not read.** Large CJK collections cost no start-up time, and only
  the pages touched use memory.
- **Glyphs are rasterised on demand** by ImGui 1.92's dynamic atlas, so no glyph ranges are
  pre-baked.
- **Missing files are skipped**, so the same build works on other phones.
- **CJK:** the variant (Japanese, Simplified or Traditional Chinese, Korean) follows the
  phone's language.
- **Weights:** since Android 12, Roboto is one variable font. `inspectPrimary` reads its named
  instances with FreeType and picks, for each weight, the instance at that `wght` with every
  other axis at its default (upright, normal width); FreeType loads it through the face index
  (`instance << 16`). Older phones have a static file per weight (`Roboto-Medium.ttf`, …). A
  weight found in neither is the regular face emboldened by FreeType.
- **Sizes are Android text sizes.** A size in sp is the em square, as Android's `textSize`, not
  ImGui's ascender-to-descender height: FreeType is asked for the nominal size (see
  `kLoaderFlags`). Lines are spaced by Roboto's own line height (hhea ascender to descender,
  1.17 em), Android's default, and every fallback font is sized the same way, as on the phone.
- **Rendering as on Android:** no hinting, and glyph advances rounded to whole pixels
  (`PixelSnapH`), so a line is as wide as the official app's (about 1 % wider than with
  fractional advances).
- **Variable CJK:** FreeType reads CFF2, so the variable Noto Sans CJK is used when present.

### Colour emoji

ImGui's rasteriser cannot read colour fonts (CBDT/sbix), so emoji come from Android itself. A
custom `ImFontLoader` (`emojiLoader` in `Fonts.cpp`), merged right after Roboto, claims the
emoji blocks (U+2300–23FF, U+2600–27BF, U+2B00–2BFF, U+1F000–1FAFF) when the system can draw
them (`Paint.hasGlyph`). Each glyph is drawn on demand by `Platform::drawGlyph` (JNI: `Paint`,
`Canvas`, `Bitmap`) at the text's size, as everywhere else on the phone, then copied
into ImGui's RGBA atlas as a coloured glyph (`ImFontGlyph::Colored`, so text colour does not
tint it). Glyphs are baked once per size, like any other.

Coming before the symbol fonts, it also draws in colour the pictographs they carry (hearts,
stars), as everywhere else on the phone. Sequence controls (zero-width joiner, variation
selectors, skin-tone modifiers, tags) take no space.

Cost on the Galaxy A14:

| Item | Measured |
| --- | --- |
| Font loading | 4–14 ms |
| Mapped font files | about 1.5 MB |
| APK size | +0.3 MB |

### Typography

Sizes and weights were measured on screenshots of the official app on the same phone: the ink
box and the inked area of each text were compared with Roboto rendered at every candidate size
and weight. They live in `Theme.hpp`:

| Role | Size | Weight |
| --- | --- | --- |
| Channel name (search card, channel page, You) | 24 sp | Bold |
| Sheet title (comments, description, accounts, save, download) | 20 sp | Bold |
| Section header (Shorts shelf, You, channel shelves), watch title | 18 sp | Bold |
| Card title | 16 sp | SemiBold |
| Shelf and Shorts tile title, post video title | 14 sp | SemiBold |
| Buttons, chips, names, "N replies" | 14 sp | Medium |
| Comment text, post text | 14 sp | Regular |
| Metadata under a card title, duration badge | 12 sp | Medium |
| Other metadata (handles, views, dates) | 12 sp | Regular |
| Navigation labels | 10 sp | Regular |

The same measurements on SlopTube's screens give the same sizes and weights.

## Pipeline (`ui/text/`)

| File | Role |
| --- | --- |
| `Utf8.hpp` | Decoding and encoding helpers |
| `UnicodeTables.*` | Compact generated tables (≈5 KB): bidi classes, Arabic joining types, mirrored brackets. Regenerate with `generate_tables.py` (Python's `unicodedata`, no downloads) |
| `Shaping.*` | Arabic contextual forms (isolated, initial, medial, final) through the presentation-form blocks; lam-alef ligatures; transparent marks (harakat) are skipped when joining |
| `Bidi.*` | A pragmatic subset of the Unicode Bidirectional Algorithm: paragraph direction from the first strong character, RTL runs, European and Arabic numbers kept left to right, neutrals resolved from their neighbours, mirrored brackets |
| `TextLayout.*` | Line breaking on the logical text, then shaping and reordering per line, with a 128-entry cache |

Strings without right-to-left characters take a fast path that skips everything above. Once
cached, a string costs nothing; its first appearance costs tens to a few hundred microseconds,
mostly ImGui rasterising new glyphs.

## Canvas API

- `measure`, `text`, `paragraph` and `line` take UTF-8 in logical order, exactly as received.
- `paragraph` and `line` take an optional `TextAlign`:
  - `Natural` (default) aligns left-to-right text left and right-to-left text right, like the
    official app;
  - `Center` centres the text.
- A truncated line gets its ellipsis at the logical end, which is the left side for Arabic.
- `Canvas::with(weight)` returns a canvas drawing in that weight. Screens use the roles of
  `Theme.hpp` (`kHeadingWeight`, `kTitleWeight`, `kLabelWeight`) rather than naming weights.
- Empty text, including a default-constructed `std::string_view`, is a no-op.

## Known limitations

- **Emoji sequences are drawn one code point at a time.** A family (joined with U+200D) shows as
  its members side by side, a skin tone falls back to the default yellow, and a flag shows as
  its two regional-indicator letters.
- **No Indic or Thai shaping.** Glyphs show, but Devanagari conjuncts and Thai stacked marks may
  be imperfect.
- **Arabic vowel marks** are not positioned from font data.
- **Bidi subset:** explicit embedding and isolate controls are ignored, and a multi-line string
  is one paragraph.
- **The search field** shows every script but does not shape or reorder while typing.
