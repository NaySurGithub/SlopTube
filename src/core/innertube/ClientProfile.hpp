#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace sloptube::innertube {

enum class ClientKind : std::uint8_t {
    Tv,
    Android,
    Ios,
    Mweb,
    VisionOs,
};

// Identity an InnerTube request presents to the server.
struct ClientProfile {
    ClientKind kind;
    std::string_view name;
    int id;
    std::string_view version;
    // Expanded by userAgent(): {version}, {model} and {os} refer to the fields below.
    std::string_view userAgentFormat;
    std::string_view deviceMake;
    std::string_view deviceModel;
    std::string_view osName;
    std::string_view osVersion;
    int androidSdkVersion;
    bool acceptsOAuth;
};

[[nodiscard]] const ClientProfile& clientProfile(ClientKind kind);

[[nodiscard]] const std::string& userAgent(ClientKind kind);

// Clients tried in order when resolving playable streams.
[[nodiscard]] std::span<const ClientKind> playerFallbackOrder();

} // namespace sloptube::innertube
