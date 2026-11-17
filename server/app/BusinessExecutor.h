#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace eat::app {

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
    struct Task {
        std::function<void()> fn;
        std::chrono::steady_clock::time_point deadline;
    };

    void Worker();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Task> queue_;
    std::vector<std::thread> workers_;
    std::size_t queue_capacity_;
    std::chrono::milliseconds task_timeout_;
    bool stopping_ = false;
    std::size_t inflight_ = 0;
    std::atomic<std::uint64_t> rejected_{0};
    std::atomic<std::uint64_t> timed_out_{0};
};

}  // namespace eat::app