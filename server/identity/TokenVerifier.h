#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "shared/protocol/protocol.h"

namespace eat::identity {

struct SessionRecord {
    std::string user_key;
    std::string session_id;
    std::uint64_t account_id = 0;
    std::uint32_t revoke_epoch = 0;
    std::uint32_t account_revoke_epoch = 0;
    int status = 0;
    std::int64_t access_expires_at_ms = 0;
};

struct VerifiedSession {
    std::string user_key;
    std::string session_id;
    std::uint64_t account_id = 0;
    std::uint32_t revoke_epoch = 0;
};

class SessionStore {
 public:
    virtual ~SessionStore() = default;
    virtual bool LoadBySessionHash(const std::string& session_hash, SessionRecord& out) = 0;
};

class TokenVerifier {
 public:
    using Clock = std::chrono::steady_clock;

    explicit TokenVerifier(SessionStore* store,
                           std::chrono::seconds cache_ttl = std::chrono::seconds(60));

    bool Verify(const std::string& token, VerifiedSession& out,
                protocol::ErrorCode& error);
    void Invalidate(const std::string& token);
    void Clear();

    static std::string Digest(const std::string& token);

 private:
    struct CacheEntry {
        VerifiedSession session;
        Clock::time_point expires;
    };

    SessionStore* store_;
    std::chrono::seconds cache_ttl_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, CacheEntry> cache_;
};

}  // namespace eat::identity