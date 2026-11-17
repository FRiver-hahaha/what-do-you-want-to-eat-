#include "server/app/BusinessExecutor.h"

namespace eat::app {

BusinessExecutor::BusinessExecutor(std::size_t threads, std::size_t queue_capacity,
                                   std::uint32_t task_timeout_ms)
    : queue_capacity_(queue_capacity), task_timeout_(task_timeout_ms) {
    for (std::size_t i = 0; i < threads; ++i) {
        workers_.emplace_back([this] { Worker(); });
    }
}

BusinessExecutor::~BusinessExecutor() {
    Stop();
}

bool BusinessExecutor::Submit(std::function<void()> task) {
    return SubmitBefore(std::move(task), std::chrono::steady_clock::now() + task_timeout_);
}

bool BusinessExecutor::SubmitBefore(std::function<void()> task,
                                    std::chrono::steady_clock::time_point deadline) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || queue_.size() >= queue_capacity_) {
            rejected_.fetch_add(1);
            return false;
        }
        queue_.push_back(Task{std::move(task), deadline});
    }
    cv_.notify_one();
    return true;
}

void BusinessExecutor::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
}

std::size_t BusinessExecutor::queue_depth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

std::size_t BusinessExecutor::inflight() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return inflight_;
}

std::uint64_t BusinessExecutor::rejected() const {
    return rejected_.load();
}

std::uint64_t BusinessExecutor::timed_out() const {
    return timed_out_.load();
}

void BusinessExecutor::Worker() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) {
                if (stopping_) return;
                continue;
            }
            task = std::move(queue_.front());
            queue_.pop_front();
            ++inflight_;
        }
        if (std::chrono::steady_clock::now() <= task.deadline) {
            task.fn();
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