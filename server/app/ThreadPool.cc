#include "server/app/ThreadPool.h"

namespace eat::app {

ThreadPool::ThreadPool(std::size_t threads, std::size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity) {
    if (threads == 0) threads = 1;
    workers_.reserve(threads);
    for (std::size_t i = 0; i < threads; ++i) {
        workers_.emplace_back([this] { Worker(); });
    }
}

ThreadPool::~ThreadPool() { Stop(); }

bool ThreadPool::Submit(std::function<void()> task) {
    return SubmitBefore(std::move(task), TimePoint::max());
}

bool ThreadPool::SubmitBefore(std::function<void()> task, TimePoint deadline) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || queue_.size() >= capacity_) {
            rejected_.fetch_add(1);
            return false;
        }
        queue_.push_back(Task{std::move(task), deadline});
    }
    cv_.notify_one();
    return true;
}

void ThreadPool::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
        dropped_.fetch_add(queue_.size());
        queue_.clear();
    }
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
}

std::size_t ThreadPool::queue_depth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

std::size_t ThreadPool::inflight() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return inflight_;
}

std::uint64_t ThreadPool::rejected() const { return rejected_.load(); }
std::uint64_t ThreadPool::timed_out() const { return timed_out_.load(); }
std::uint64_t ThreadPool::executed() const { return executed_.load(); }
std::uint64_t ThreadPool::failed() const { return failed_.load(); }
std::uint64_t ThreadPool::dropped() const { return dropped_.load(); }

void ThreadPool::Worker() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            task = std::move(queue_.front());
            queue_.pop_front();
            ++inflight_;
        }
        if (std::chrono::steady_clock::now() <= task.deadline) {
            try {
                task.fn();
                executed_.fetch_add(1);
            } catch (...) {
                failed_.fetch_add(1);
            }
        } else {
            timed_out_.fetch_add(1);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            --inflight_;
        }
    }
}

}  // namespace eat::app
