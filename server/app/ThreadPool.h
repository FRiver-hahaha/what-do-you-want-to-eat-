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

// Bounded worker pool. Tasks are queued FIFO; submission is non-blocking and
// fails fast when the queue is full or the pool is stopping. Exceptions thrown
// by a task are contained so one bad task cannot terminate the process.
class ThreadPool {
 public:
    using TimePoint = std::chrono::steady_clock::time_point;

    ThreadPool(std::size_t threads, std::size_t capacity);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    bool Submit(std::function<void()> task);
    bool SubmitBefore(std::function<void()> task, TimePoint deadline);
    void Stop();

    std::size_t queue_depth() const;
    std::size_t inflight() const;
    std::uint64_t rejected() const;
    std::uint64_t timed_out() const;
    std::uint64_t executed() const;
    std::uint64_t failed() const;
    std::uint64_t dropped() const;

 private:
    struct Task {
        std::function<void()> fn;
        TimePoint deadline;
    };

    void Worker();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Task> queue_;
    std::vector<std::thread> workers_;
    std::size_t capacity_;
    bool stopping_ = false;
    std::size_t inflight_ = 0;
    std::atomic<std::uint64_t> rejected_{0};
    std::atomic<std::uint64_t> timed_out_{0};
    std::atomic<std::uint64_t> executed_{0};
    std::atomic<std::uint64_t> failed_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace eat::app
