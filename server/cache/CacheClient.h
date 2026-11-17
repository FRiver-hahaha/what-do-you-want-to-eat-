#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "server/cache/CacheKeyBuilder.h"
#include "server/database/RedisClient.h"
#include "shared/protocol/JsonValue.h"

namespace eat::cache {

struct CacheOptions {
    std::uint32_t breaker_failures = 3;
    std::uint32_t breaker_open_ms = 5000;
};

// Cache-Aside façade: Redis failures are fail-open (callers fall through to
// MySQL). Only the whitelisted GET/SETEX/DEL/MGET/EXISTS/PING commands are used.
class CacheClient {
 public:
    CacheClient(db::RedisClient& redis, CacheKeyBuilder& builder,
                const CacheOptions& options);

    bool Read(const std::string& key, protocol::JsonValue& out);
    bool Write(const std::string& key, const protocol::JsonValue& data,
               const std::string& domain);
    bool WriteEmpty(const std::string& key, const std::string& domain);
    void Delete(const std::vector<std::string>& keys);
    bool Available();

    const CacheKeyBuilder& keys() const { return builder_; }
    db::RedisClient& redis() { return redis_; }

    std::uint64_t ProfileVersion(const std::string& user_key);
    void BumpProfileVersion(const std::string& user_key);

 private:
    bool BreakerOpen();
    void RecordResult(bool ok);
    std::uint32_t TtlFor(const std::string& key, const std::string& domain,
                         bool negative) const;

    db::RedisClient& redis_;
    CacheKeyBuilder& builder_;
    CacheOptions options_;

    std::mutex breaker_mutex_;
    std::uint32_t failures_ = 0;
    std::chrono::steady_clock::time_point open_until_;

    std::mutex version_mutex_;
    std::unordered_map<std::string, std::uint64_t> versions_;
};

}  // namespace eat::cache
