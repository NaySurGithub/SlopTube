#include "app/ui/text/Shaping.hpp"

#include "app/ui/text/Bidi.hpp"
#include "app/ui/text/UnicodeTables.hpp"

#include <algorithm>
#include <cstdint>

namespace sloptube::app::text {
namespace {

constexpr char32_t kLam = 0x0644;
constexpr char32_t kTatweel = 0x0640;
constexpr char32_t kZeroWidthJoiner = 0x200D;

bool g_enabled = true;

enum class Joining : std::uint8_t {
    None,        // U: does not join.
    Right,       // R: joins to the preceding letter only.
    Dual,        // D: joins on both sides.
    Causing,     // C: tatweel, ZWJ.
    Transparent, // T: marks, skipped when finding neighbours.
};

const ArabicForms* formsOf(char32_t cp) {
    if (cp > 0xFFFF) {
        return nullptr;
    }
    const auto found =
        std::ranges::lower_bound(kArabicForms, static_cast<char16_t>(cp), {}, &ArabicForms::base);
    return found != kArabicForms.end() && char32_t{found->base} == cp ? &*found : nullptr;
}

const LamAlef* lamAlefOf(char32_t alef) {
    const auto found = std::ranges::find(kLamAlef, static_cast<char16_t>(alef), &LamAlef::alef);
    return found != kLamAlef.end() ? &*found : nullptr;
}

Joining joiningOf(char32_t cp) {
    if (cp == kTatweel || cp == kZeroWidthJoiner) {
        return Joining::Causing;
    }
    if (const ArabicForms* forms = formsOf(cp)) {
        if (forms->initial != 0 || forms->medial != 0) {
            return Joining::Dual;
        }
        return forms->final != 0 ? Joining::Right : Joining::None;
    }
    const BidiClass c = bidiClass(cp);
    return c == BidiClass::NSM || (c == BidiClass::BN && cp != 0x200C) ? Joining::Transparent
                                                                       : Joining::None;
}

bool joinsForward(Joining j) {
    return j == Joining::Dual || j == Joining::Causing;
}

bool joinsBackward(Joining j) {
    return j == Joining::Dual || j == Joining::Right || j == Joining::Causing;
}

} // namespace

void setArabicShapingEnabled(bool enabled) {
    g_enabled = enabled;
}

bool shapeArabic(std::u32string& text) {
    if (!g_enabled) {
        return false;
    }
    const std::size_t n = text.size();
    bool changed = false;
    std::size_t out = 0;
    // Joining type of the last non-transparent character written, i.e. the one to our right.
    Joining previous = Joining::None;

    for (std::size_t i = 0; i < n; ++i) {
        const char32_t cp = text[i];
        const Joining joining = joiningOf(cp);
        if (joining == Joining::Transparent) {
            text[out++] = cp;
            continue;
        }

        std::size_t next = i + 1;
        while (next < n && joiningOf(text[next]) == Joining::Transparent) {
            ++next;
        }
        const bool joinsPrevious = joinsForward(previous) && joinsBackward(joining);
        const ArabicForms* forms = formsOf(cp);

        if (cp == kLam && i + 1 < n) {
            if (const LamAlef* ligature = lamAlefOf(text[i + 1])) {
                text[out++] = joinsPrevious ? ligature->final : ligature->isolated;
                previous = Joining::Right; // The ligature ends with alef, which is right-joining.
                changed = true;
                ++i;
                continue;
            }
        }

        if (forms != nullptr) {
            const bool joinsNext =
                joinsForward(joining) && next < n && joinsBackward(joiningOf(text[next]));
            char16_t shaped = forms->isolated;
            if (joinsPrevious && joinsNext) {
                shaped = forms->medial != 0 ? forms->medial : forms->final;
            } else if (joinsPrevious) {
                shaped = forms->final;
            } else if (joinsNext) {
                shaped = forms->initial;
            }
            if (shaped != 0) {
                text[out++] = shaped;
                changed = true;
            } else {
                text[out++] = cp;
            }
        } else {
            text[out++] = cp;
        }
        previous = joining;
    }
    text.resize(out);
    return changed;
}

} // namespace sloptube::app::text
