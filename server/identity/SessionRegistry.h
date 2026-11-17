#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace eat::identity {

class SessionRegistry {
 public:
    using Clock = std::chrono::steady_clock;

    explicit SessionRegistry(std::size_t max_devices = 3);

    bool Bind(const std::string& user_key, std::uint64_t conn_id,
              const std::string& device_id, std::uint64_t& replaced_conn_id);
    bool Unbind(const std::string& user_key, std::uint64_t conn_id);
    bool IsOnline(const std::string& user_key) const;
    std::size_t ActiveDevices(const std::string& user_key) const;
    std::uint64_t NextEventSeq(const std::string& user_key);
    std::size_t OnlineUsers() const;

 private:
    struct Entry {
        std::uint64_t conn_id = 0;
        std::string device_id;
        Clock::time_point since = Clock::now();
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<Entry>> by_user_;
    std::unordered_map<std::string, std::uint64_t> event_seq_;
    std::size_t max_devices_;
};

}  // namespace eat::identity