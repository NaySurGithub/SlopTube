#pragma once

#include "core/util/Result.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sloptube::http {

inline constexpr std::string_view kJsonContentType = "Content-Type: application/json";
// Media URLs take their byte range as a query parameter ("&range=first-last"); the Range header
// is throttled by the CDN, which also caps a range at kMaxRangeBytes.
inline constexpr std::string_view kRangeParameter = "&range=";
inline constexpr std::int64_t kMaxRangeBytes = 10 * 1024 * 1024;
// Local files (downloaded videos) are read through the same interface: "file:///path?..." with
// an optional range parameter, so players and caption fetches need no special case.
inline constexpr std::string_view kFileScheme = "file://";
// Prefix of the error message of a failed transfer (no network, timeout), as opposed to an
// error answered by the server.
inline constexpr std::string_view kTransportError = "http: ";

enum class Method : std::uint8_t {
    Get,
    Post,
};

struct Request {
    Method method = Method::Get;
    std::string url;
    std::vector<std::string> headers; // Raw "Name: value" lines.
    std::string body;
    // Safe to send twice (a read). When the server stays silent for kStallSeconds the request is
    // sent again once on a fresh connection, instead of waiting out a stalled one.
    bool retryOnStall = false;
    // The transfer is abandoned, within about a second, once this becomes true.
    const std::atomic<bool>* cancelled = nullptr;
};

// A request that receives nothing for this long has stalled: on the phone's Wi-Fi, a connection
// sometimes goes silent for 5-20 s while a new one answers in a fraction of a second.
inline constexpr long kStallSeconds = 5;

struct Response {
    long status = 0;
    std::string body;

    [[nodiscard]] bool ok() const { return status >= 200 && status < 300; }
};

struct Options {
    std::string caPath; // Directory of trusted CA certificates; empty uses the TLS backend default.
};

// Thread-safe HTTP client. Each calling thread keeps its own connection, while DNS, TLS sessions
// and the connection pool are shared across threads.
class Client {
public:
    explicit Client(Options options = {});
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    [[nodiscard]] Result<Response> send(const Request& request) const;

private:
    struct Shared;
    std::unique_ptr<Shared> m_shared;
};

} // namespace sloptube::http
