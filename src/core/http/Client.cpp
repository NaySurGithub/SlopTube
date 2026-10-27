#include "core/http/Client.hpp"

#include <curl/curl.h>

#include <array>
#include <charconv>
#include <fstream>
#include <mutex>

namespace sloptube::http {
namespace {

constexpr long kConnectTimeoutMs = 10'000;
constexpr long kLowSpeedLimitBytes = 1;
constexpr long kLowSpeedTimeSeconds = 20;

struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }

    ~CurlGlobal() { curl_global_cleanup(); }
};

struct EasyHandle {
    CURL* handle = curl_easy_init();

    ~EasyHandle() { curl_easy_cleanup(handle); }
};

std::size_t appendBody(char* data, std::size_t size, std::size_t count, void* user) {
    static_cast<std::string*>(user)->append(data, size * count);
    return size * count;
}

// Serves a "file://" URL from disk, honouring its range parameter.
Result<Response> readFile(std::string_view url) {
    url.remove_prefix(kFileScheme.size());
    const auto query = url.find('?');
    const std::string path(url.substr(0, query));
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return Response{.status = 404};
    }
    const std::int64_t size = file.tellg();
    std::int64_t first = 0;
    std::int64_t last = size - 1;
    if (query != std::string_view::npos) {
        // The range parameter is also accepted as the query's first one ("?range=").
        std::string_view parameters = url.substr(query);
        constexpr std::string_view kName = kRangeParameter.substr(1);
        if (const auto at = parameters.find(kName); at != std::string_view::npos) {
            const std::string_view range = parameters.substr(at + kName.size());
            const char* end = range.data() + range.size();
            auto [dash, error] = std::from_chars(range.data(), end, first);
            if (error == std::errc{} && dash != end && *dash == '-') {
                std::from_chars(dash + 1, end, last);
            }
        }
    }
    last = std::min(last, size - 1);
    if (first < 0 || first > last) {
        return Response{.status = first == size ? 206 : 416};
    }
    Response response{.status = 206};
    response.body.resize(static_cast<std::size_t>(last - first + 1));
    file.seekg(first);
    file.read(response.body.data(), static_cast<std::streamsize>(response.body.size()));
    if (!file) {
        return fail("file: read failed");
    }
    return response;
}

} // namespace

struct Client::Shared {
    Options options;
    CURLSH* share = nullptr;
    std::array<std::mutex, CURL_LOCK_DATA_LAST> locks;

    static void lock(CURL*, curl_lock_data data, curl_lock_access, void* user) {
        static_cast<Shared*>(user)->locks[data].lock();
    }

    static void unlock(CURL*, curl_lock_data data, void* user) {
        static_cast<Shared*>(user)->locks[data].unlock();
    }
};

Client::Client(Options options)
    : m_shared(std::make_unique<Shared>()) {
    static const CurlGlobal global;
    m_shared->options = std::move(options);
    m_shared->share = curl_share_init();
    curl_share_setopt(m_shared->share, CURLSHOPT_LOCKFUNC, &Shared::lock);
    curl_share_setopt(m_shared->share, CURLSHOPT_UNLOCKFUNC, &Shared::unlock);
    curl_share_setopt(m_shared->share, CURLSHOPT_USERDATA, m_shared.get());
    for (const auto data :
         {CURL_LOCK_DATA_DNS, CURL_LOCK_DATA_SSL_SESSION, CURL_LOCK_DATA_CONNECT}) {
        curl_share_setopt(m_shared->share, CURLSHOPT_SHARE, data);
    }
}

Client::~Client() {
    curl_share_cleanup(m_shared->share);
}

Result<Response> Client::send(const Request& request) const {
    if (request.url.starts_with(kFileScheme)) {
        return readFile(request.url);
    }
    thread_local EasyHandle easy;
    CURL* curl = easy.handle;
    curl_easy_reset(curl);

    curl_slist* headers = nullptr;
    for (const auto& header : request.headers) {
        headers = curl_slist_append(headers, header.c_str());
    }

    Response response;
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_SHARE, m_shared->share);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    // IPv4 first: on the target phone's Wi-Fi, InnerTube answered the same search in 2.8-5.5 s
    // over IPv6 against 1.1-1.8 s over IPv4 (a desktop on that network saw no difference).
    // Requests fall back to IPv6 when there is no IPv4 route.
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMs);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, kLowSpeedLimitBytes);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME,
                     request.retryOnStall ? kStallSeconds : kLowSpeedTimeSeconds);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &appendBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    if (!m_shared->options.caPath.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAPATH, m_shared->options.caPath.c_str());
    }
    if (request.method == Method::Post) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(request.body.size()));
    }

    CURLcode code = curl_easy_perform(curl);
    if (code == CURLE_COULDNT_RESOLVE_HOST || code == CURLE_COULDNT_CONNECT) {
        // No IPv4 route (an IPv6-only network): use whatever the network offers.
        response = {};
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER);
        code = curl_easy_perform(curl);
    }
    if (code == CURLE_OPERATION_TIMEDOUT && request.retryOnStall) {
        response = {};
        curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, kLowSpeedTimeSeconds);
        code = curl_easy_perform(curl);
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    // Detach so the thread-local handle never outlives the share it points to.
    curl_easy_setopt(curl, CURLOPT_SHARE, nullptr);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, nullptr);
    curl_slist_free_all(headers);
    if (code != CURLE_OK) {
        return fail(std::string(kTransportError) + curl_easy_strerror(code));
    }
    return response;
}

} // namespace sloptube::http
