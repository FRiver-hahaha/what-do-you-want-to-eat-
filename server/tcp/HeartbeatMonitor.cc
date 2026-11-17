#include "server/tcp/HeartbeatMonitor.h"

namespace eat::tcp {

HeartbeatMonitor::HeartbeatMonitor(std::chrono::seconds idle_timeout)
    : idle_timeout_(idle_timeout) {}

void HeartbeatMonitor::OnFrame(std::uint64_t conn_id, Clock::time_point now) {
    latest_[conn_id] = now;
    heap_.push(Entry{now, conn_id});
}

void HeartbeatMonitor::Remove(std::uint64_t conn_id) {
    latest_.erase(conn_id);
}

std::vector<std::uint64_t> HeartbeatMonitor::Sweep(Clock::time_point now) {
    std::vector<std::uint64_t> expired;
    const auto cutoff = now - idle_timeout_;
    while (!heap_.empty() && heap_.top().last < cutoff) {
        const Entry entry = heap_.top();
        heap_.pop();
        const auto it = latest_.find(entry.conn_id);
        if (it == latest_.end() || it->second != entry.last) {
            continue;
        }
        expired.push_back(entry.conn_id);
        latest_.erase(it);
    }
    return expired;
}

std::size_t HeartbeatMonitor::tracked() const {
    return latest_.size();
}

}  // namespace eat::tcp