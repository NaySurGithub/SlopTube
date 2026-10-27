#include "core/auth/Session.hpp"

#include "core/http/Client.hpp"
#include "core/innertube/ClientProfile.hpp"
#include "core/json/Json.hpp"

#include <array>
#include <fstream>
#include <random>
#include <sstream>

namespace sloptube::auth {
namespace {

constexpr std::string_view kTvPageUrl = "https://www.youtube.com/tv";
constexpr std::string_view kDeviceCodeUrl = "https://www.youtube.com/o/oauth2/device/code";
constexpr std::string_view kTokenUrl = "https://www.youtube.com/o/oauth2/token";
constexpr std::string_view kScope =
    "http://gdata.youtube.com https://www.googleapis.com/auth/youtube";
constexpr std::string_view kDeviceModel = "ytlr::";
constexpr std::string_view kDeviceGrant = "http://oauth.net/grant_type/device/1.0";
constexpr std::string_view kRefreshGrant = "refresh_token";

// Refresh this long before the server-side expiry to absorb clock skew and request latency.
constexpr std::int64_t kExpiryMarginSeconds = 120;

std::int64_t unixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string randomUuid() {
    std::random_device device;
    std::mt19937_64 engine((static_cast<std::uint64_t>(device()) << 32) |
                           static_cast<std::uint64_t>(device()));
    std::array<std::uint8_t, 16> bytes{};
    for (auto& byte : bytes) {
        byte = static_cast<std::uint8_t>(engine());
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40); // Version 4.
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80); // RFC 4122 variant.

    static constexpr std::string_view kHex = "0123456789abcdef";
    std::string uuid;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            uuid += '-';
        }
        uuid += kHex[bytes[i] >> 4];
        uuid += kHex[bytes[i] & 0xF];
    }
    return uuid;
}

// Reads the quoted value following `marker` in `source`, starting at `from`.
std::optional<std::pair<std::string, std::size_t>>
quotedAfter(std::string_view source, std::string_view marker, std::size_t from) {
    const auto start = source.find(marker, from);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    const auto valueStart = start + marker.size();
    const auto valueEnd = source.find('"', valueStart);
    if (valueEnd == std::string_view::npos) {
        return std::nullopt;
    }
    return std::pair{std::string(source.substr(valueStart, valueEnd - valueStart)), valueEnd};
}

Result<std::string> postJson(const http::Client& http, std::string_view url, std::string body) {
    auto response = http.send({
        .method = http::Method::Post,
        .url = std::string(url),
        .headers = {std::string(http::kJsonContentType)},
        .body = std::move(body),
    });
    if (!response) {
        return std::unexpected(response.error());
    }
    return std::move(response->body);
}

} // namespace

Session::Session(const http::Client& http, std::filesystem::path storageFile)
    : m_http(http)
    , m_storageFile(std::move(storageFile)) {
    load();
}

bool Session::signedIn() const {
    const std::scoped_lock lock(m_mutex);
    return !m_stored.accounts.empty();
}

Session::Credentials* Session::active() {
    return m_stored.active < m_stored.accounts.size() ? &m_stored.accounts[m_stored.active]
                                                      : nullptr;
}

std::vector<innertube::Account> Session::accounts() const {
    const std::scoped_lock lock(m_mutex);
    std::vector<innertube::Account> result;
    result.reserve(m_stored.accounts.size());
    for (const auto& account : m_stored.accounts) {
        result.push_back(account.profile);
    }
    return result;
}

std::size_t Session::activeAccount() const {
    const std::scoped_lock lock(m_mutex);
    return m_stored.active;
}

void Session::switchAccount(std::size_t index) {
    const std::scoped_lock lock(m_mutex);
    if (index < m_stored.accounts.size()) {
        m_stored.active = index;
        save();
    }
}

void Session::setProfile(innertube::Account profile) {
    const std::scoped_lock lock(m_mutex);
    auto* current = active();
    if (current == nullptr) {
        return;
    }
    current->profile = std::move(profile);
    const auto& byline = current->profile.byline;
    for (std::size_t i = 0; i < m_stored.accounts.size(); ++i) {
        if (i != m_stored.active && !byline.empty() &&
            m_stored.accounts[i].profile.byline == byline) {
            m_stored.accounts.erase(m_stored.accounts.begin() + static_cast<std::ptrdiff_t>(i));
            if (i < m_stored.active) {
                --m_stored.active;
            }
            break;
        }
    }
    save();
}

Result<DeviceCode> Session::beginSignIn() {
    const std::scoped_lock lock(m_mutex);
    if (auto ready = ensureClientCredentials(); !ready) {
        return std::unexpected(ready.error());
    }

    auto body = json::Writer()
                    .beginObject()
                    .field("client_id", m_stored.clientId)
                    .field("scope", kScope)
                    .field("device_id", m_stored.deviceId)
                    .field("device_model", kDeviceModel)
                    .endObject()
                    .take();
    auto response = postJson(m_http, kDeviceCodeUrl, std::move(body));
    if (!response) {
        return std::unexpected(response.error());
    }
    auto root = json::parse(*response);
    if (!root) {
        return std::unexpected(root.error());
    }

    DeviceCode code{
        .deviceCode = std::string(json::string(*root, "/device_code")),
        .userCode = std::string(json::string(*root, "/user_code")),
        .verificationUrl = std::string(json::string(*root, "/verification_url")),
        .pollInterval = std::chrono::seconds(json::integer(*root, "/interval")),
        .expiresAt = std::chrono::steady_clock::now() +
                     std::chrono::seconds(json::integer(*root, "/expires_in")),
    };
    if (code.deviceCode.empty()) {
        return fail("auth: device code request rejected");
    }
    return code;
}

Result<PollState> Session::pollSignIn(const DeviceCode& code) {
    if (std::chrono::steady_clock::now() >= code.expiresAt) {
        return PollState::Expired;
    }

    const std::scoped_lock lock(m_mutex);
    auto body = json::Writer()
                    .beginObject()
                    .field("client_id", m_stored.clientId)
                    .field("client_secret", m_stored.clientSecret)
                    .field("code", code.deviceCode)
                    .field("grant_type", kDeviceGrant)
                    .endObject()
                    .take();
    auto response = postJson(m_http, kTokenUrl, std::move(body));
    if (!response) {
        return std::unexpected(response.error());
    }
    auto root = json::parse(*response);
    if (!root) {
        return std::unexpected(root.error());
    }

    if (const auto error = json::string(*root, "/error"); !error.empty()) {
        if (error == "authorization_pending") {
            return PollState::Pending;
        }
        if (error == "slow_down") {
            return PollState::SlowDown;
        }
        if (error == "expired_token") {
            return PollState::Expired;
        }
        return PollState::Denied;
    }

    m_stored.accounts.push_back(Credentials{
        .refreshToken = std::string(json::string(*root, "/refresh_token")),
        .accessToken = std::string(json::string(*root, "/access_token")),
        .expiresAt = unixNow() + json::integer(*root, "/expires_in"),
    });
    m_stored.active = m_stored.accounts.size() - 1;
    save();
    return PollState::Authorized;
}

void Session::signOut() {
    const std::scoped_lock lock(m_mutex);
    if (active() == nullptr) {
        return;
    }
    m_stored.accounts.erase(m_stored.accounts.begin() +
                            static_cast<std::ptrdiff_t>(m_stored.active));
    if (m_stored.active >= m_stored.accounts.size()) {
        m_stored.active = 0;
    }
    save();
}

Result<std::string> Session::authorization() {
    const std::scoped_lock lock(m_mutex);
    const auto* current = active();
    if (current == nullptr) {
        return fail("auth: not signed in");
    }
    if (unixNow() + kExpiryMarginSeconds >= current->expiresAt) {
        if (auto refreshed = refresh(); !refreshed) {
            return std::unexpected(refreshed.error());
        }
    }
    return "Bearer " + current->accessToken;
}

Result<void> Session::ensureClientCredentials() {
    if (m_stored.deviceId.empty()) {
        m_stored.deviceId = randomUuid();
    }
    if (!m_stored.clientId.empty() && !m_stored.clientSecret.empty()) {
        return {};
    }

    // The TV web app embeds its OAuth client; reading it at sign-in keeps us in sync with it.
    auto page = m_http.send({
        .url = std::string(kTvPageUrl),
        .headers = {"User-Agent: " + innertube::userAgent(innertube::ClientKind::Tv)},
    });
    if (!page) {
        return std::unexpected(page.error());
    }
    const auto id = quotedAfter(page->body, "clientId:\"", 0);
    if (!id) {
        return fail("auth: OAuth client id not found on the TV page");
    }
    // The secret is the next quoted property, whose minified name changes between releases.
    const auto secret = quotedAfter(page->body, ":\"", id->second + 1);
    if (!secret) {
        return fail("auth: OAuth client secret not found on the TV page");
    }
    m_stored.clientId = id->first;
    m_stored.clientSecret = secret->first;
    return {};
}

Result<void> Session::refresh() {
    auto& current = *active();
    auto body = json::Writer()
                    .beginObject()
                    .field("client_id", m_stored.clientId)
                    .field("client_secret", m_stored.clientSecret)
                    .field("refresh_token", current.refreshToken)
                    .field("grant_type", kRefreshGrant)
                    .endObject()
                    .take();
    auto response = postJson(m_http, kTokenUrl, std::move(body));
    if (!response) {
        return std::unexpected(response.error());
    }
    auto root = json::parse(*response);
    if (!root) {
        return std::unexpected(root.error());
    }
    const auto token = json::string(*root, "/access_token");
    if (token.empty()) {
        return fail("auth: token refresh rejected");
    }
    current.accessToken = token;
    current.expiresAt = unixNow() + json::integer(*root, "/expires_in");
    save();
    return {};
}

void Session::load() {
    std::ifstream file(m_storageFile, std::ios::binary);
    if (!file) {
        return;
    }
    std::string content{std::istreambuf_iterator<char>(file), {}};
    auto root = json::parse(content);
    if (!root) {
        return;
    }
    m_stored = Stored{
        .clientId = std::string(json::string(*root, "/clientId")),
        .clientSecret = std::string(json::string(*root, "/clientSecret")),
        .deviceId = std::string(json::string(*root, "/deviceId")),
        .accounts = {},
        .active = static_cast<std::size_t>(json::integer(*root, "/active")),
    };
    const auto readAccount = [&](json::Element node) {
        if (json::string(node, "/refreshToken").empty()) {
            return;
        }
        m_stored.accounts.push_back(Credentials{
            .refreshToken = std::string(json::string(node, "/refreshToken")),
            .accessToken = std::string(json::string(node, "/accessToken")),
            .expiresAt = json::integer(node, "/expiresAt"),
            .profile =
                {
                    .name = std::string(json::string(node, "/name")),
                    .byline = std::string(json::string(node, "/byline")),
                    .handle = std::string(json::string(node, "/handle")),
                    .photo = std::string(json::string(node, "/photo")),
                },
        });
    };
    simdjson::dom::array accounts;
    if (!(*root)["accounts"].get_array().get(accounts)) {
        for (const auto account : accounts) {
            readAccount(account);
        }
    } else {
        readAccount(*root); // Single-account layout of earlier versions.
    }
}

void Session::save() const {
    json::Writer writer;
    writer.beginObject()
        .field("clientId", m_stored.clientId)
        .field("clientSecret", m_stored.clientSecret)
        .field("deviceId", m_stored.deviceId)
        .field("active", static_cast<std::int64_t>(m_stored.active));
    writer.beginArray("accounts");
    for (const auto& account : m_stored.accounts) {
        writer.beginObject()
            .field("refreshToken", account.refreshToken)
            .field("accessToken", account.accessToken)
            .field("expiresAt", account.expiresAt)
            .field("name", account.profile.name)
            .field("byline", account.profile.byline)
            .field("handle", account.profile.handle)
            .field("photo", account.profile.photo)
            .endObject();
    }
    writer.endArray().endObject();
    const auto content = writer.take();

    std::error_code error;
    std::filesystem::create_directories(m_storageFile.parent_path(), error);
    auto staging = m_storageFile;
    staging += ".tmp";
    {
        std::ofstream file(staging, std::ios::binary | std::ios::trunc);
        file << content;
    }
    std::filesystem::permissions(
        staging, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, error);
    std::filesystem::rename(staging, m_storageFile, error);
}

} // namespace sloptube::auth
