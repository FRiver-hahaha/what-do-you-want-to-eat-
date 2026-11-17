#include "server/tcp/RateLimiter.h"

namespace eat::tcp {

bool RateLimiter::Allow(const std::string& key, std::uint32_t rate_per_s,
                        std::uint32_t burst, Clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& bucket = buckets_[key];
    if (!bucket.initialized || bucket.last > now) {
        bucket.last = now;
        bucket.tokens = static_cast<double>(burst);
        bucket.initialized = true;
    }
    const double elapsed =
        std::chrono::duration<double>(now - bucket.last).count();
    bucket.last = now;
    bucket.tokens += elapsed * static_cast<double>(rate_per_s);
    if (bucket.tokens > static_cast<double>(burst)) {
        bucket.tokens = static_cast<double>(burst);
    }
    if (bucket.tokens < 1.0) {
        return false;
    }
    bucket.tokens -= 1.0;
    return true;
}

void RateLimiter::Remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    buckets_.erase(key);
}

std::size_t RateLimiter::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buckets_.size();
}

}  // namespace eat::tcp