#include "server/tcp/Backpressure.h"

namespace eat::tcp {

Backpressure::Backpressure(std::size_t per_conn_high, std::size_t per_conn_low,
                           std::size_t global_high)
    : per_conn_high_(per_conn_high), per_conn_low_(per_conn_low), global_high_(global_high) {}

bool Backpressure::OnQueued(std::size_t bytes) {
    const std::size_t total = global_queued_.fetch_add(bytes) + bytes;
    return total <= global_high_;
}

void Backpressure::OnDrained(std::size_t bytes) {
    const std::size_t current = global_queued_.load();
    const std::size_t next = current > bytes ? current - bytes : 0;
    global_queued_.store(next);
}

bool Backpressure::ShouldPauseRead(std::size_t conn_queued_bytes) const {
    return conn_queued_bytes >= per_conn_high_;
}

bool Backpressure::ShouldResumeRead(std::size_t conn_queued_bytes) const {
    return conn_queued_bytes <= per_conn_low_;
}

std::size_t Backpressure::global_queued() const {
    return global_queued_.load();
}

}  // namespace eat::tcp