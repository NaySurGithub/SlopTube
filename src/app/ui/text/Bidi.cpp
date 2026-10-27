#include "app/ui/text/Bidi.hpp"

#include "app/ui/text/UnicodeTables.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace sloptube::app::text {
namespace {

constexpr unsigned kClassBits = 4;
constexpr std::uint32_t kClassMask = (1u << kClassBits) - 1;

// Blocks holding right-to-left scripts (Hebrew, Arabic, Syriac, Thaana, NKo, ... and their
// presentation forms); a cheap superset test before any real classification.
constexpr std::array<std::pair<char32_t, char32_t>, 5> kRtlBlocks{{
    {0x0590, 0x08FF},
    {0xFB1D, 0xFDFF},
    {0xFE70, 0xFEFF},
    {0x10800, 0x10FFF},
    {0x1E800, 0x1EFFF},
}};

struct MirrorPair {
    char32_t open;
    char32_t close;
    bool bracket; // Paired by rule N0 (Bidi_Paired_Bracket_Type).
};

// Bidi_Mirroring_Glyph pairs for the characters that occur in practice.
constexpr std::array<MirrorPair, 10> kMirrors{{
    {U'(', U')', true},
    {U'[', U']', true},
    {U'{', U'}', true},
    {U'\u3008', U'\u3009', true}, // Angle brackets.
    {U'\u300A', U'\u300B', true}, // Double angle brackets.
    {U'\uFF08', U'\uFF09', true}, // Fullwidth parentheses.
    {U'<', U'>', false},
    {U'\u00AB', U'\u00BB', false}, // Guillemets.
    {U'\u2039', U'\u203A', false}, // Single guillemets.
    {U'\u2264', U'\u2265', false}, // Less/greater than or equal.
}};

// Brackets considered for pairing; deeper nesting is left to the neutral rules (BD16 uses 63).
constexpr std::size_t kMaxBracketDepth = 16;

// Invisible marks that only steer the algorithm; dropped from the visual output.
bool isDirectionMark(char32_t cp) {
    return cp == 0x200E || cp == 0x200F || cp == 0x061C;
}

char32_t mirror(char32_t cp) {
    for (const MirrorPair& pair : kMirrors) {
        if (cp == pair.open) {
            return pair.close;
        }
        if (cp == pair.close) {
            return pair.open;
        }
    }
    return cp;
}

bool isStrong(BidiClass c) {
    return c == BidiClass::L || c == BidiClass::R || c == BidiClass::AL;
}

bool isNeutral(BidiClass c) {
    return c == BidiClass::B || c == BidiClass::S || c == BidiClass::WS || c == BidiClass::ON;
}

// Direction a resolved class exerts on neighbouring neutrals (N1): numbers act as R.
BidiClass strongDirection(BidiClass c) {
    return c == BidiClass::L ? BidiClass::L : BidiClass::R;
}

// Direction a resolved class gives to a bracket pair, or ON for none.
BidiClass bracketDirection(BidiClass c) {
    if (c == BidiClass::L) {
        return BidiClass::L;
    }
    return c == BidiClass::R || c == BidiClass::EN || c == BidiClass::AN ? BidiClass::R
                                                                         : BidiClass::ON;
}

// N0: paired brackets take the direction of their content, preferring the embedding direction,
// so "(abc)" inside right-to-left text stays together. `pairs` is scratch storage.
void resolveBrackets(std::u32string_view text, std::vector<BidiClass>& t, BidiClass embedding,
                     std::vector<std::pair<std::size_t, std::size_t>>& pairs) {
    // BD16: match brackets with a bounded stack of (expected closer, opening index).
    std::array<std::pair<char32_t, std::size_t>, kMaxBracketDepth> stack{};
    std::size_t depth = 0;
    pairs.clear();
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (t[i] != BidiClass::ON) {
            continue;
        }
        const auto pair = std::ranges::find_if(kMirrors, [&](const MirrorPair& m) {
            return m.bracket && (m.open == text[i] || m.close == text[i]);
        });
        if (pair == kMirrors.end()) {
            continue;
        }
        if (pair->open == text[i]) {
            if (depth == stack.size()) {
                break;
            }
            stack[depth++] = {pair->close, i};
            continue;
        }
        for (std::size_t level = depth; level-- > 0;) {
            if (stack[level].first == text[i]) {
                pairs.emplace_back(stack[level].second, i);
                depth = level;
                break;
            }
        }
    }
    std::ranges::sort(pairs);

    for (const auto [open, close] : pairs) {
        BidiClass opposite = BidiClass::ON;
        BidiClass resolved = BidiClass::ON;
        for (std::size_t k = open + 1; k < close && resolved == BidiClass::ON; ++k) {
            const BidiClass d = bracketDirection(t[k]);
            if (d == embedding) {
                resolved = embedding;
            } else if (d != BidiClass::ON) {
                opposite = d;
            }
        }
        if (resolved == BidiClass::ON && opposite != BidiClass::ON) {
            BidiClass before = embedding;
            for (std::size_t k = open; k-- > 0;) {
                if (const BidiClass d = bracketDirection(t[k]); d != BidiClass::ON) {
                    before = d;
                    break;
                }
            }
            resolved = before == opposite ? opposite : embedding;
        }
        if (resolved != BidiClass::ON) {
            t[open] = resolved;
            t[close] = resolved;
        }
    }
}

} // namespace

BidiClass bidiClass(char32_t cp) {
    // First run whose start is greater than cp, then step back to the run containing it.
    const auto run =
        std::ranges::upper_bound(kBidiRuns, static_cast<std::uint32_t>(cp), std::less{},
                                 [](std::uint32_t r) { return r >> kClassBits; });
    return static_cast<BidiClass>(*(run - 1) & kClassMask);
}

bool containsRtl(std::string_view utf8) {
    for (std::size_t i = 0; i < utf8.size(); ++i) {
        const auto lead = static_cast<unsigned char>(utf8[i]);
        if (lead < 0xC0) {
            continue; // ASCII or continuation byte.
        }
        const std::size_t length = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
        char32_t value = lead & (lead >= 0xF0 ? 0x07u : lead >= 0xE0 ? 0x0Fu : 0x1Fu);
        for (std::size_t k = 1; k < length && i + k < utf8.size(); ++k) {
            value = (value << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3Fu);
        }
        for (const auto& [first, last] : kRtlBlocks) {
            if (value >= first && value <= last) {
                return true;
            }
        }
    }
    return false;
}

bool isRtlParagraph(std::u32string_view text) {
    for (const char32_t cp : text) {
        const BidiClass c = bidiClass(cp);
        if (c == BidiClass::B) {
            break;
        }
        if (isStrong(c)) {
            return c != BidiClass::L;
        }
    }
    return false;
}

void BidiReorderer::reorder(std::u32string_view line, bool rtlParagraph, std::u32string& visual) {
    // X9: embedding/isolate controls and other boundary neutrals are removed up front.
    m_text.clear();
    m_classes.clear();
    for (const char32_t cp : line) {
        const BidiClass c = bidiClass(cp);
        if (c != BidiClass::BN) {
            m_text.push_back(cp);
            m_classes.push_back(c);
        }
    }
    const std::size_t n = m_text.size();
    const BidiClass sos = rtlParagraph ? BidiClass::R : BidiClass::L;
    auto& t = m_classes;

    // W1: marks take the class of what they modify.
    for (std::size_t i = 0; i < n; ++i) {
        if (t[i] == BidiClass::NSM) {
            t[i] = i == 0 ? sos : t[i - 1];
        }
    }
    // W2-W3: European digits after Arabic letters become Arabic numbers; AL becomes R.
    BidiClass lastStrong = sos;
    for (std::size_t i = 0; i < n; ++i) {
        if (isStrong(t[i])) {
            lastStrong = t[i];
        } else if (t[i] == BidiClass::EN && lastStrong == BidiClass::AL) {
            t[i] = BidiClass::AN;
        }
        if (t[i] == BidiClass::AL) {
            t[i] = BidiClass::R;
        }
    }
    // W4: a single separator between two numbers of the same kind joins them.
    for (std::size_t i = 1; i + 1 < n; ++i) {
        const BidiClass before = t[i - 1];
        if (before != t[i + 1]) {
            continue;
        }
        if ((t[i] == BidiClass::ES && before == BidiClass::EN) ||
            (t[i] == BidiClass::CS && (before == BidiClass::EN || before == BidiClass::AN))) {
            t[i] = before;
        }
    }
    // W5: terminators (%, $, ...) adjacent to European numbers become numbers.
    for (std::size_t i = 0; i < n;) {
        if (t[i] != BidiClass::ET) {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < n && t[end] == BidiClass::ET) {
            ++end;
        }
        if ((i > 0 && t[i - 1] == BidiClass::EN) || (end < n && t[end] == BidiClass::EN)) {
            std::fill(t.begin() + static_cast<std::ptrdiff_t>(i),
                      t.begin() + static_cast<std::ptrdiff_t>(end), BidiClass::EN);
        }
        i = end;
    }
    // W6-W7: leftover separators are neutral; European numbers in left-to-right context are L.
    lastStrong = sos;
    for (std::size_t i = 0; i < n; ++i) {
        if (t[i] == BidiClass::ES || t[i] == BidiClass::ET || t[i] == BidiClass::CS) {
            t[i] = BidiClass::ON;
        } else if (t[i] == BidiClass::L || t[i] == BidiClass::R) {
            lastStrong = t[i];
        } else if (t[i] == BidiClass::EN && lastStrong == BidiClass::L) {
            t[i] = BidiClass::L;
        }
    }
    resolveBrackets(m_text, t, sos, m_brackets);
    // N1-N2: neutrals between equal directions take it, otherwise the paragraph direction.
    for (std::size_t i = 0; i < n;) {
        if (!isNeutral(t[i])) {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < n && isNeutral(t[end])) {
            ++end;
        }
        const BidiClass before = i == 0 ? sos : strongDirection(t[i - 1]);
        const BidiClass after = end == n ? sos : strongDirection(t[end]);
        std::fill(t.begin() + static_cast<std::ptrdiff_t>(i),
                  t.begin() + static_cast<std::ptrdiff_t>(end), before == after ? before : sos);
        i = end;
    }

    // I1-I2: implicit levels.
    const std::uint8_t base = rtlParagraph ? 1 : 0;
    m_levels.assign(n, base);
    for (std::size_t i = 0; i < n; ++i) {
        const BidiClass c = t[i];
        if (base == 0) {
            m_levels[i] = c == BidiClass::R                            ? 1
                          : (c == BidiClass::EN || c == BidiClass::AN) ? 2
                                                                       : 0;
        } else {
            m_levels[i] = c == BidiClass::R ? 1 : 2;
        }
    }
    // L1: segment separators and trailing whitespace return to the paragraph level.
    bool trailing = true;
    for (std::size_t i = n; i-- > 0;) {
        const BidiClass original = bidiClass(m_text[i]);
        const bool space = original == BidiClass::WS || original == BidiClass::S;
        if (original == BidiClass::S || original == BidiClass::B || (trailing && space)) {
            m_levels[i] = base;
        }
        trailing = trailing && space;
    }

    // L2: reverse every run at or above each level, from the highest down to the lowest odd.
    const std::uint8_t highest = n == 0 ? 0 : *std::ranges::max_element(m_levels);
    for (std::uint8_t level = highest; level >= 1; --level) {
        for (std::size_t i = 0; i < n;) {
            if (m_levels[i] < level) {
                ++i;
                continue;
            }
            std::size_t end = i;
            while (end < n && m_levels[end] >= level) {
                ++end;
            }
            std::reverse(m_text.begin() + static_cast<std::ptrdiff_t>(i),
                         m_text.begin() + static_cast<std::ptrdiff_t>(end));
            std::reverse(m_levels.begin() + static_cast<std::ptrdiff_t>(i),
                         m_levels.begin() + static_cast<std::ptrdiff_t>(end));
            i = end;
        }
    }

    // L4: mirrored glyphs in right-to-left runs.
    visual.clear();
    for (std::size_t i = 0; i < n; ++i) {
        const char32_t cp = m_text[i];
        if (!isDirectionMark(cp)) {
            visual.push_back((m_levels[i] & 1) != 0 ? mirror(cp) : cp);
        }
    }
}

} // namespace sloptube::app::text
