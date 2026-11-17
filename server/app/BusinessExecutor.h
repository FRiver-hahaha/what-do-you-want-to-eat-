#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "server/app/ThreadPool.h"

namespace eat::app {

// Business-facing facade over ThreadPool: keeps the per-task deadline policy
// and exposes the counters used by connection governance tests/metrics.
class BusinessExecutor {
 public:
    BusinessExecutor(std::size_t threads, std::size_t queue_capacity,
                     std::uint32_t task_timeout_ms);
    ~BusinessExecutor();

    bool Submit(std::function<void()> task);
    bool SubmitBefore(std::function<void()> task, std::chrono::steady_clock::time_point deadline);
    void Stop();

    std::size_t queue_depth() const;
    std::size_t inflight() const;
    std::uint64_t rejected() const;
    std::uint64_t timed_out() const;

 private:
    ThreadPool pool_;
    std::chrono::milliseconds task_timeout_;
};

}  // namespace eat::app