#include "server/app/BusinessExecutor.h"

namespace eat::app {

BusinessExecutor::BusinessExecutor(std::size_t threads, std::size_t queue_capacity,
                                   std::uint32_t task_timeout_ms)
    : pool_(threads, queue_capacity), task_timeout_(task_timeout_ms) {}

BusinessExecutor::~BusinessExecutor() { Stop(); }

bool BusinessExecutor::Submit(std::function<void()> task) {
    return SubmitBefore(std::move(task), std::chrono::steady_clock::now() + task_timeout_);
}

bool BusinessExecutor::SubmitBefore(std::function<void()> task,
                                    std::chrono::steady_clock::time_point deadline) {
    return pool_.SubmitBefore(std::move(task), deadline);
}

void BusinessExecutor::Stop() { pool_.Stop(); }

std::size_t BusinessExecutor::queue_depth() const { return pool_.queue_depth(); }
std::size_t BusinessExecutor::inflight() const { return pool_.inflight(); }
std::uint64_t BusinessExecutor::rejected() const { return pool_.rejected(); }
std::uint64_t BusinessExecutor::timed_out() const { return pool_.timed_out(); }

}  // namespace eat::app