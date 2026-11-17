#include "server/cache/CacheClient.h"

#include "server/cache/CacheValue.h"
#include "server/cache/TtlPolicy.h"

namespace eat::cache {
namespace {

std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::uint64_t SeedOf(const std::string& key) {
    std::uint64_t seed = 1469598103934665603ULL;
    for (const unsigned char c : key) {
        seed ^= c;
        seed *= 1099511628211ULL;
    }
    return seed;
}

}  // namespace

CacheClient::CacheClient(db::RedisClient& redis, CacheKeyBuilder& builder,
                         const CacheOptions& options)
    : redis_(redis), builder_(builder), options_(options) {}

bool CacheClient::BreakerOpen() {
    std::lock_guard<std::mutex> lock(breaker_mutex_);
    if (open_until_ == std::chrono::steady_clock::time_point()) return false;
    if (std::chrono::steady_clock::now() >= open_until_) {
        open_until_ = std::chrono::steady_clock::time_point();
        failures_ = 0;
        return false;
    }
    return true;
}

void CacheClient::RecordResult(bool ok) {
    std::lock_guard<std::mutex> lock(breaker_mutex_);
    if (ok) {
        failures_ = 0;
        open_until_ = std::chrono::steady_clock::time_point();
        return;
    }
    ++failures_;
    if (failures_ >= options_.breaker_failures) {
        open_until_ = std::chrono::steady_clock::now() +
                      std::chrono::milliseconds(options_.breaker_open_ms);
    }
}

bool CacheClient::Available() {
    if (BreakerOpen()) return false;
    const bool ok = redis_.Ping();
    RecordResult(ok);
    return ok;
}

bool CacheClient::Read(const std::string& key, protocol::JsonValue& out) {
    if (BreakerOpen()) return false;
    std::string raw;
    const db::RedisClient::GetResult result = redis_.Get(key, raw);
    if (result == db::RedisClient::GetResult::Error) {
        RecordResult(false);
        return false;
    }
    RecordResult(true);
    if (result == db::RedisClient::GetResult::Miss) return false;

    CacheValue value;
    std::string reason;
    if (!CacheValue::Parse(raw, value, reason)) return false;
    if (value.empty) return false;
    out = value.data;
    return true;
}

std::uint32_t CacheClient::TtlFor(const std::string& key, const std::string& domain,
                                  bool negative) const {
    const TtlRule* rule = cache::TtlFor(domain);
    if (rule == nullptr) return 0;
    const std::uint32_t base = negative ? rule->negative_s : rule->positive_s;
    if (base == 0) return 0;
    const double jitter = negative ? rule->negative_jitter : rule->positive_jitter;
    return ApplyJitter(base, jitter, SeedOf(key));
}

bool CacheClient::Write(const std::string& key, const protocol::JsonValue& data,
                        const std::string& domain) {
    if (BreakerOpen()) return false;
    const std::uint32_t ttl = TtlFor(key, domain, false);
    if (ttl == 0) return false;

    CacheValue value;
    value.gen = 1;
    value.empty = false;
    value.ts_ms = static_cast<std::int64_t>(NowMs());
    value.data = data;

    const bool ok = redis_.SetEx(key, ttl, value.Serialize());
    RecordResult(ok);
    return ok;
}

bool CacheClient::WriteEmpty(const std::string& key, const std::string& domain) {
    if (BreakerOpen()) return false;
    const std::uint32_t ttl = TtlFor(key, domain, true);
    if (ttl == 0) return false;

    CacheValue value;
    value.gen = 1;
    value.empty = true;
    value.ts_ms = static_cast<std::int64_t>(NowMs());
    const bool ok = redis_.SetEx(key, ttl, value.Serialize());
    RecordResult(ok);
    return ok;
}

void CacheClient::Delete(const std::vector<std::string>& keys) {
    if (keys.empty() || BreakerOpen()) return;
    RecordResult(redis_.Del(keys));
}

std::uint64_t CacheClient::ProfileVersion(const std::string& user_key) {
    std::lock_guard<std::mutex> lock(version_mutex_);
    const auto found = versions_.find(user_key);
    return found == versions_.end() ? 1 : found->second;
}

void CacheClient::BumpProfileVersion(const std::string& user_key) {
    std::lock_guard<std::mutex> lock(version_mutex_);
    std::uint64_t& value = versions_[user_key];
    if (value == 0) value = 1;
    ++value;
}

}  // namespace eat::cache
