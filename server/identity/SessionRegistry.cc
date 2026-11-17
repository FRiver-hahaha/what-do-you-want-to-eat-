#include "server/identity/SessionRegistry.h"

#include <algorithm>

namespace eat::identity {

SessionRegistry::SessionRegistry(std::size_t max_devices) : max_devices_(max_devices) {}

bool SessionRegistry::Bind(const std::string& user_key, std::uint64_t conn_id,
                           const std::string& device_id, std::uint64_t& replaced_conn_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& entries = by_user_[user_key];
    replaced_conn_id = 0;
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it->device_id == device_id) {
            replaced_conn_id = it->conn_id;
            entries.erase(it);
            break;
        }
    }
    if (entries.size() >= max_devices_) {
        return false;
    }
    entries.push_back(Entry{conn_id, device_id, Clock::now()});
    return true;
}

bool SessionRegistry::Unbind(const std::string& user_key, std::uint64_t conn_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = by_user_.find(user_key);
    if (it == by_user_.end()) return false;
    auto& entries = it->second;
    const auto before = entries.size();
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [conn_id](const Entry& entry) {
                                     return entry.conn_id == conn_id;
                                 }),
                  entries.end());
    if (entries.empty()) {
        by_user_.erase(it);
    }
    return entries.size() != before;
}

bool SessionRegistry::IsOnline(const std::string& user_key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = by_user_.find(user_key);
    return it != by_user_.end() && !it->second.empty();
}

std::size_t SessionRegistry::ActiveDevices(const std::string& user_key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = by_user_.find(user_key);
    return it == by_user_.end() ? 0 : it->second.size();
}

std::uint64_t SessionRegistry::NextEventSeq(const std::string& user_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    return ++event_seq_[user_key];
}

std::size_t SessionRegistry::OnlineUsers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return by_user_.size();
}

}  // namespace eat::identity