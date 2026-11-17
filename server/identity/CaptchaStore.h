#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace eat::identity {

// In-memory, single-use graphical captcha store. The SVG is generated on the
// server; clients only receive the challenge id and the rendered image.
class CaptchaStore {
 public:
    struct Challenge {
        std::string id;
        std::string svg;
        std::uint32_t expires_in_s = 120;
    };

    explicit CaptchaStore(std::uint32_t ttl_s = 120);

    Challenge Issue();
    bool Verify(const std::string& id, const std::string& code);

 private:
    struct Entry {
        std::string code;
        std::chrono::steady_clock::time_point expires;
    };

    void PruneLocked();

    std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
    std::uint32_t ttl_s_;
    std::size_t max_entries_ = 20000;
};

}  // namespace eat::identity
