#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace sloptube::protobuf {

// Minimal protobuf encoder for the opaque `params` fields InnerTube expects in some requests.
class Writer {
public:
    Writer& varint(std::uint32_t field, std::uint64_t value);
    Writer& bytes(std::uint32_t field, std::string_view value);

    Writer& message(std::uint32_t field, const Writer& nested) {
        return bytes(field, nested.m_out);
    }

    [[nodiscard]] const std::string& data() const { return m_out; }

private:
    void raw(std::uint64_t value);

    std::string m_out;
};

// URL-safe base64 with padding, as InnerTube encodes params.
[[nodiscard]] std::string base64Url(std::string_view data);

} // namespace sloptube::protobuf
