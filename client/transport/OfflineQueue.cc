#include "client/transport/OfflineQueue.h"

#include <chrono>
#include <algorithm>

namespace eat::client {

OfflineQueue::OfflineQueue(std::size_t capacity) : capacity_(capacity) {}

bool OfflineQueue::Enqueue(const std::string& idem_key, const std::string& method,
                           const protocol::JsonValue& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& item : queue_) {
        if (item.idem_key == idem_key) return false;
    }
    if (queue_.size() >= capacity_) return false;
    QueuedWrite write;
    write.idem_key = idem_key;
    write.method = method;
    write.params = params;
    write.created_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    queue_.push_back(std::move(write));
    return true;
}

bool OfflineQueue::Peek(QueuedWrite& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) return false;
    out = queue_.front();
    return true;
}

void OfflineQueue::MarkDone(const std::string& idem_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                                [&idem_key](const QueuedWrite& item) {
                                    return item.idem_key == idem_key;
                                }),
                 queue_.end());
}

void OfflineQueue::MarkFailed(const std::string& idem_key, const std::string& error,
                              int max_retries) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& item : queue_) {
        if (item.idem_key != idem_key) continue;
        item.attempts += 1;
        item.last_error = error;
        if (item.attempts >= max_retries) {
            needs_manual_ += 1;
        }
        return;
    }
}

std::size_t OfflineQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

bool OfflineQueue::Full() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size() >= capacity_;
}

std::size_t OfflineQueue::NeedsManual() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return needs_manual_;
}

}  // namespace eat::client