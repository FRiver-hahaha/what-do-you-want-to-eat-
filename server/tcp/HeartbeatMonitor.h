#pragma once

#include <chrono>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <vector>

namespace eat::tcp {

class HeartbeatMonitor {
 public:
    using Clock = std::chrono::steady_clock;

    explicit HeartbeatMonitor(std::chrono::seconds idle_timeout = std::chrono::seconds(165));

    void OnFrame(std::uint64_t conn_id, Clock::time_point now);
    void Remove(std::uint64_t conn_id);
    std::vector<std::uint64_t> Sweep(Clock::time_point now);
    std::size_t tracked() const;

 private:
    struct Entry {
        Clock::time_point last;
        std::uint64_t conn_id;
    };
    struct Later {
        bool operator()(const Entry& a, const Entry& b) const { return a.last > b.last; }
    };

    std::priority_queue<Entry, std::vector<Entry>, Later> heap_;
    std::unordered_map<std::uint64_t, Clock::time_point> latest_;
    std::chrono::seconds idle_timeout_;
};

}  // namespace eat::tcp