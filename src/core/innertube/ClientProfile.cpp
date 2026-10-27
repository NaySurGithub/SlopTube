#include "core/innertube/ClientProfile.hpp"

#include <array>
#include <utility>

namespace sloptube::innertube {
namespace {

// Versions match the shipping apps: TV from youtube.com/tv, Android from the current Play Store
// release.
// The profile order must follow ClientKind.
constexpr std::array kProfiles{
    ClientProfile{
        .kind = ClientKind::Tv,
        .name = "TVHTML5",
        .id = 7,
        .version = "7.20261025.19.00",
        .userAgentFormat = "Mozilla/5.0 (ChromiumStylePlatform) Cobalt/25.lts.30.1034943-gold "
                           "(unlike Gecko), Unknown_TV_Unknown_0/Unknown (Unknown, Unknown)",
        .deviceMake = {},
        .deviceModel = {},
        .osName = {},
        .osVersion = {},
        .androidSdkVersion = 0,
        .acceptsOAuth = true,
    },
    ClientProfile{
        .kind = ClientKind::Android,
        .name = "ANDROID",
        .id = 3,
        .version = "21.42.243",
        .userAgentFormat = "com.google.android.youtube/{version} (Linux; U; Android {os}) gzip",
        .deviceMake = {},
        .deviceModel = {},
        .osName = "Android",
        .osVersion = "15",
        .androidSdkVersion = 35,
        .acceptsOAuth = false,
    },
    ClientProfile{
        .kind = ClientKind::Ios,
        .name = "IOS",
        .id = 5,
        .version = "20.10.4",
        .userAgentFormat =
            "com.google.ios.youtube/{version} ({model}; U; CPU iOS 18_3_2 like Mac OS X;)",
        .deviceMake = "Apple",
        .deviceModel = "iPhone16,2",
        .osName = "iPhone",
        .osVersion = "18.3.2.22D82",
        .androidSdkVersion = 0,
        .acceptsOAuth = false,
    },
    ClientProfile{
        .kind = ClientKind::Mweb,
        .name = "MWEB",
        .id = 2,
        .version = "2.20261023.01.00",
        .userAgentFormat =
            "Mozilla/5.0 (Linux; Android {os}) AppleWebKit/537.36 (KHTML, like Gecko) "
            "Chrome/141.0.0.0 Mobile Safari/537.36",
        .deviceMake = {},
        .deviceModel = {},
        .osName = {},
        .osVersion = "15",
        .androidSdkVersion = 0,
        .acceptsOAuth = false,
    },
    ClientProfile{
        .kind = ClientKind::VisionOs,
        .name = "VISIONOS",
        .id = 101,
        .version = "0.1",
        // Vision Pro's Safari presents itself as desktop Safari.
        .userAgentFormat = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 "
                           "(KHTML, like Gecko) Version/18.0 Safari/605.1.15",
        .deviceMake = {},
        .deviceModel = {},
        .osName = {},
        .osVersion = {},
        .androidSdkVersion = 0,
        .acceptsOAuth = false,
    },
};

// Without a PO token, TV refuses playback and the ANDROID, ANDROID_VR and IOS adaptive streams
// (direct URLs and SABR alike) stop after the first minute of media. VISIONOS serves complete
// adaptive streams but needs visitorData, so ANDROID's progressive 360p stream backs it up.
constexpr std::array kPlayerOrder{ClientKind::VisionOs, ClientKind::Android};

std::string expandUserAgent(const ClientProfile& profile) {
    const std::array<std::pair<std::string_view, std::string_view>, 3> tokens{{
        {"{version}", profile.version},
        {"{model}", profile.deviceModel},
        {"{os}", profile.osVersion},
    }};
    std::string result(profile.userAgentFormat);
    for (const auto& [token, value] : tokens) {
        if (const auto at = result.find(token); at != std::string::npos) {
            result.replace(at, token.size(), value);
        }
    }
    return result;
}

} // namespace

const ClientProfile& clientProfile(ClientKind kind) {
    return kProfiles[std::to_underlying(kind)];
}

const std::string& userAgent(ClientKind kind) {
    static const auto agents = [] {
        std::array<std::string, kProfiles.size()> expanded;
        for (std::size_t i = 0; i < kProfiles.size(); ++i) {
            expanded[i] = expandUserAgent(kProfiles[i]);
        }
        return expanded;
    }();
    return agents[std::to_underlying(kind)];
}

std::span<const ClientKind> playerFallbackOrder() {
    return kPlayerOrder;
}

} // namespace sloptube::innertube
