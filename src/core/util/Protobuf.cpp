#include "core/util/Protobuf.hpp"

namespace sloptube::protobuf {
namespace {

enum WireType : std::uint32_t {
    kVarint = 0,
    kLengthDelimited = 2,
};

constexpr std::string_view kUrlAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
constexpr std::string_view kStandardAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encode(std::string_view data, std::string_view alphabet);

} // namespace

Writer& Writer::varint(std::uint32_t field, std::uint64_t value) {
    raw((field << 3) | kVarint);
    raw(value);
    return *this;
}

Writer& Writer::bytes(std::uint32_t field, std::string_view value) {
    raw((field << 3) | kLengthDelimited);
    raw(value.size());
    m_out += value;
    return *this;
}

void Writer::raw(std::uint64_t value) {
    while (value >= 0x80) {
        m_out += static_cast<char>((value & 0x7F) | 0x80);
        value >>= 7;
    }
    m_out += static_cast<char>(value);
}

std::string base64Url(std::string_view data) {
    return encode(data, kUrlAlphabet);
}

std::string base64(std::string_view data) {
    return encode(data, kStandardAlphabet);
}

namespace {

std::string encode(std::string_view data, std::string_view alphabet) {
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const auto chunk =
            (static_cast<std::uint32_t>(static_cast<unsigned char>(data[i])) << 16) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(data[i + 1])) << 8) |
            static_cast<unsigned char>(data[i + 2]);
        out += alphabet[(chunk >> 18) & 0x3F];
        out += alphabet[(chunk >> 12) & 0x3F];
        out += alphabet[(chunk >> 6) & 0x3F];
        out += alphabet[chunk & 0x3F];
    }
    if (const auto rest = data.size() - i; rest > 0) {
        auto chunk = static_cast<std::uint32_t>(static_cast<unsigned char>(data[i])) << 16;
        if (rest == 2) {
            chunk |= static_cast<std::uint32_t>(static_cast<unsigned char>(data[i + 1])) << 8;
        }
        out += alphabet[(chunk >> 18) & 0x3F];
        out += alphabet[(chunk >> 12) & 0x3F];
        out += rest == 2 ? alphabet[(chunk >> 6) & 0x3F] : '=';
        out += '=';
    }
    return out;
}

} // namespace

} // namespace sloptube::protobuf
