#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace eat::tcp {

class RateLimiter {
 public:
    using Clock = std::chrono::steady_clock;

    bool Allow(const std::string& key, std::uint32_t rate_per_s,
               std::uint32_t burst, Clock::time_point now);
    void Remove(const std::string& key);
    std::size_t size() const;

 private:
    struct Bucket {
        double tokens = 0.0;
        Clock::time_point last = Clock::now();
        bool initialized = false;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
};

}  // namespace eat::tcp