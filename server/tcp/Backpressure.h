#pragma once

#include <atomic>
#include <cstddef>

namespace eat::tcp {

class Backpressure {
 public:
    Backpressure(std::size_t per_conn_high, std::size_t per_conn_low,
                 std::size_t global_high);

    bool OnQueued(std::size_t bytes);
    void OnDrained(std::size_t bytes);
    bool ShouldPauseRead(std::size_t conn_queued_bytes) const;
    bool ShouldResumeRead(std::size_t conn_queued_bytes) const;
    std::size_t global_queued() const;

 private:
    std::size_t per_conn_high_;
    std::size_t per_conn_low_;
    std::size_t global_high_;
    std::atomic<std::size_t> global_queued_{0};
};

}  // namespace eat::tcp