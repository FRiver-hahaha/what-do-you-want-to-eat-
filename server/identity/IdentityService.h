#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "server/database/MySQLPool.h"
#include "server/identity/TokenVerifier.h"

namespace eat::identity {

enum class IdentityStatus {
    Ok,
    InvalidParams,
    DuplicateLoginId,
    InvalidCredentials,
    Locked,
    Disabled,
    Unavailable,
};

struct LoginTokens {
    std::string user_key;
    std::string access_token;
    std::string refresh_token;
    std::uint64_t account_id = 0;
};

// Unified identity: register/login/logout/verify. Tokens are opaque random
// 32-byte values; only their SHA-256 digests are stored. Passwords use scrypt.
class IdentityService : public SessionStore {
 public:
    explicit IdentityService(db::MySQLPool& pool) : pool_(pool) {}

    IdentityStatus Register(const std::string& login_id, const std::string& password,
                            const std::string& display_name, LoginTokens& out);
    IdentityStatus Login(const std::string& login_id, const std::string& password,
                         const std::string& device_id, const std::string& client_version,
                         LoginTokens& out);
    bool Logout(const std::string& access_token, std::string& error);

    bool UserExists(const std::string& user_key);

    bool LoadBySessionHash(const std::string& session_hash, SessionRecord& out) override;

 private:
    db::MySQLPool& pool_;
};

// Exposed for tests and for the migration tooling.
std::string NewUserKey();
std::string NewOpaqueToken();

}  // namespace eat::identity