#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

#include "shared/protocol/JsonValue.h"

namespace eat::client {

struct QueuedWrite {
    std::string idem_key;
    std::string method;
    protocol::JsonValue params;
    std::int64_t created_ms = 0;
    int attempts = 0;
    std::string last_error;
};

class OfflineQueue {
 public:
    explicit OfflineQueue(std::size_t capacity = 1000);

    bool Enqueue(const std::string& idem_key, const std::string& method,
                 const protocol::JsonValue& params);
    bool Peek(QueuedWrite& out) const;
    void MarkDone(const std::string& idem_key);
    void MarkFailed(const std::string& idem_key, const std::string& error,
                    int max_retries);
    std::size_t size() const;
    bool Full() const;
    std::size_t NeedsManual() const;

 private:
    mutable std::mutex mutex_;
    std::deque<QueuedWrite> queue_;
    std::size_t capacity_;
    std::size_t needs_manual_ = 0;
};

}  // namespace eat::client