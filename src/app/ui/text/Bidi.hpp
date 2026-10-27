#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Pragmatic subset of the Unicode Bidirectional Algorithm (UAX #9): one paragraph level, no
// explicit embeddings or isolates (their controls are dropped), weak and neutral resolution
// (W1-W7, N0 bracket pairs, N1-N2), implicit levels, per-line reordering (L1-L2) and mirroring.
namespace sloptube::app::text {

// Order is shared with generate_tables.py.
enum class BidiClass : std::uint8_t {
    L,
    R,
    AL,
    EN,
    ES,
    ET,
    AN,
    CS,
    NSM,
    BN,
    B,
    S,
    WS,
    ON,
};

[[nodiscard]] BidiClass bidiClass(char32_t cp);

// True when `utf8` contains a right-to-left letter; text without one never needs reordering.
[[nodiscard]] bool containsRtl(std::string_view utf8);

// Paragraph direction from the first strong character (P2-P3), left-to-right by default.
[[nodiscard]] bool isRtlParagraph(std::u32string_view text);

// Reorders one line of logical text into visual (left-to-right drawing) order.
class BidiReorderer {
public:
    void reorder(std::u32string_view line, bool rtlParagraph, std::u32string& visual);

private:
    std::vector<BidiClass> m_classes;
    std::vector<std::uint8_t> m_levels;
    std::vector<std::pair<std::size_t, std::size_t>> m_brackets;
    std::u32string m_text;
};

} // namespace sloptube::app::text
