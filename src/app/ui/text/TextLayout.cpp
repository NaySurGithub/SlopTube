#include "app/ui/text/TextLayout.hpp"

#include "app/ui/text/Bidi.hpp"
#include "app/ui/text/Shaping.hpp"
#include "app/ui/text/Utf8.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>

namespace sloptube::app::text {
namespace {

// Comfortably more than the strings visible at once, each possibly laid out at two widths.
constexpr std::size_t kCacheCapacity = 128;

struct Key {
    std::string text;
    const ImFont* font = nullptr;
    float size = 0;
    float width = 0;
    int maxLines = 0;

    bool matches(const ImFont* f, float s, std::string_view t, float w, int m) const {
        return font == f && size == s && width == w && maxLines == m && text == t;
    }
};

struct Entry {
    std::size_t hash = 0;
    std::uint64_t lastUse = 0;
    Key key;
    Paragraph paragraph;
};

// UI-thread only. Least recently used entry is recycled, keeping its buffers.
class Cache {
public:
    const Paragraph* find(std::size_t hash, const ImFont* font, float size, std::string_view text,
                          float width, int maxLines) {
        ++m_clock;
        for (Entry& entry : m_entries) {
            if (entry.hash == hash && entry.key.matches(font, size, text, width, maxLines)) {
                entry.lastUse = m_clock;
                return &entry.paragraph;
            }
        }
        return nullptr;
    }

    Entry& insert(std::size_t hash) {
        Entry* slot = nullptr;
        if (m_entries.size() < kCacheCapacity) {
            slot = &m_entries.emplace_back();
        } else {
            slot = &*std::ranges::min_element(m_entries, {}, &Entry::lastUse);
        }
        slot->hash = hash;
        slot->lastUse = m_clock;
        slot->paragraph.lines.clear();
        return *slot;
    }

private:
    std::vector<Entry> m_entries;
    std::uint64_t m_clock = 0;
};

Cache g_cache;
BidiReorderer g_reorderer;

} // namespace

const Paragraph* layoutBidi(ImFont* font, float size, std::string_view text, float width,
                            int maxLines) {
    if (!containsRtl(text)) {
        return nullptr;
    }
    const std::size_t hash = std::hash<std::string_view>{}(text);
    if (const Paragraph* cached = g_cache.find(hash, font, size, text, width, maxLines)) {
        return cached;
    }

    std::u32string codepoints;
    decodeUtf8(text, codepoints);
    shapeArabic(codepoints);
    std::string logical;
    encodeUtf8(codepoints, logical);

    Entry& entry = g_cache.insert(hash);
    entry.key = {std::string(text), font, size, width, maxLines};
    Paragraph& paragraph = entry.paragraph;
    paragraph.rtl = isRtlParagraph(codepoints);

    std::u32string visual;
    wrapLines(font, size, logical, width, maxLines, [&](std::string_view line, bool truncated) {
        codepoints.clear();
        decodeUtf8(line, codepoints);
        if (truncated) {
            decodeUtf8(kEllipsis, codepoints);
        }
        g_reorderer.reorder(codepoints, paragraph.rtl, visual);
        VisualLine& out = paragraph.lines.emplace_back();
        encodeUtf8(visual, out.text);
        out.width = font->CalcTextSizeA(size, FLT_MAX, 0, out.text.data(),
                                        out.text.data() + out.text.size())
                        .x;
    });
    return &paragraph;
}

} // namespace sloptube::app::text
