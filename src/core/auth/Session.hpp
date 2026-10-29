#pragma once

#include "core/innertube/Types.hpp"
#include "core/util/Result.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace sloptube::http {
class Client;
}

namespace sloptube::auth {

struct DeviceCode {
    std::string deviceCode;
    std::string userCode;        // Shown to the user.
    std::string verificationUrl; // Where the user enters `userCode`.
    std::chrono::seconds pollInterval{5};
    std::chrono::steady_clock::time_point expiresAt;
};

enum class PollState : std::uint8_t {
    Pending,
    SlowDown,
    Authorized,
    Denied,
    Expired,
};

// Google sign-in through the YouTube TV device flow. Several Google accounts can be signed in at
// once; requests use the active one. Credentials persist to `storageFile`, and access tokens are
// refreshed transparently.
class Session {
public:
    Session(const http::Client& http, std::filesystem::path storageFile);

    [[nodiscard]] bool signedIn() const;

    [[nodiscard]] Result<DeviceCode> beginSignIn();
    // On authorization the new account is added and becomes the active one.
    [[nodiscard]] Result<PollState> pollSignIn(const DeviceCode& code);
    // Signs the active account out; the next remaining account, if any, becomes active.
    void signOut();

    // Profiles of the signed-in accounts, in sign-in order. Profiles are filled by setProfile().
    [[nodiscard]] std::vector<innertube::Account> accounts() const;
    [[nodiscard]] std::size_t activeAccount() const;
    void switchAccount(std::size_t index);
    // Records who the active account is. Signing in to an account twice keeps only the newest.
    void setProfile(innertube::Account profile);

    // "Bearer <token>" header value, refreshing the token if it is about to expire.
    [[nodiscard]] Result<std::string> authorization();

private:
    struct Credentials {
        std::string refreshToken;
        std::string accessToken;
        std::int64_t expiresAt = 0; // Unix seconds.
        innertube::Account profile;
    };

    struct Stored {
        std::string clientId;
        std::string clientSecret;
        std::string deviceId;
        std::vector<Credentials> accounts;
        std::size_t active = 0;
    };

    [[nodiscard]] Credentials* active();

    [[nodiscard]] Result<void> ensureClientCredentials();

    struct Token {
        std::string accessToken;
        std::int64_t expiresAt = 0; // Unix seconds.
    };

    // Exchanges a refresh token for an access token; touches no state, so it runs unlocked.
    [[nodiscard]] Result<Token> requestToken(const std::string& clientId,
                                             const std::string& clientSecret,
                                             const std::string& refreshToken) const;
    void load();
    void save() const;

    const http::Client& m_http;
    std::filesystem::path m_storageFile;
    mutable std::mutex m_mutex;
    std::condition_variable m_refreshDone;
    bool m_refreshing = false; // Under m_mutex.
    Stored m_stored;
};

} // namespace sloptube::auth
