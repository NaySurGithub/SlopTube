#pragma once

#include <string>
#include <string_view>

namespace sloptube::app::text {

// Appends the code points of `utf8` to `out`. Malformed bytes decode as U+FFFD.
inline void decodeUtf8(std::string_view utf8, std::u32string& out) {
    const auto* p = reinterpret_cast<const unsigned char*>(utf8.data());
    const auto* const end = p + utf8.size();
    while (p < end) {
        const unsigned lead = *p++;
        int extra = 0;
        char32_t cp = lead;
        if (lead >= 0xF0) {
            extra = 3;
            cp = lead & 0x07;
        } else if (lead >= 0xE0) {
            extra = 2;
            cp = lead & 0x0F;
        } else if (lead >= 0xC0) {
            extra = 1;
            cp = lead & 0x1F;
        } else if (lead >= 0x80) {
            cp = 0xFFFD;
        }
        for (; extra > 0; --extra) {
            if (p == end || (*p & 0xC0) != 0x80) {
                cp = 0xFFFD;
                break;
            }
            cp = (cp << 6) | (*p++ & 0x3F);
        }
        out.push_back(cp);
    }
}

inline void encodeUtf8(char32_t cp, std::string& out) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

inline void encodeUtf8(std::u32string_view text, std::string& out) {
    for (const char32_t cp : text) {
        encodeUtf8(cp, out);
    }
}

} // namespace sloptube::app::text
