#include <atomic>
#include <cassert>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "server/app/BusinessExecutor.h"
#include "server/tcp/Backpressure.h"
#include "server/tcp/ConnContext.h"
#include "server/tcp/HeartbeatMonitor.h"
#include "server/tcp/RateLimiter.h"

namespace {

void TestHeartbeat() {
    using Clock = std::chrono::steady_clock;
    eat::tcp::HeartbeatMonitor monitor(std::chrono::seconds(165));
    const auto start = Clock::now();
    monitor.OnFrame(1, start);
    assert(monitor.Sweep(start + std::chrono::seconds(100)).empty());
    const auto expired = monitor.Sweep(start + std::chrono::seconds(166));
    assert(expired.size() == 1 && expired[0] == 1);
    assert(monitor.tracked() == 0);
}

void TestHeartbeatLazyInvalidation() {
    using Clock = std::chrono::steady_clock;
    eat::tcp::HeartbeatMonitor monitor(std::chrono::seconds(165));
    const auto start = Clock::now();
    monitor.OnFrame(2, start);
    monitor.OnFrame(2, start + std::chrono::seconds(160));
    assert(monitor.Sweep(start + std::chrono::seconds(166)).empty());
    const auto expired = monitor.Sweep(start + std::chrono::seconds(326));
    assert(expired.size() == 1 && expired[0] == 2);
}

void TestRateLimiter() {
    using Clock = std::chrono::steady_clock;
    eat::tcp::RateLimiter limiter;
    const auto now = Clock::now();
    assert(limiter.Allow("k", 10, 2, now));
    assert(limiter.Allow("k", 10, 2, now));
    assert(!limiter.Allow("k", 10, 2, now));
    assert(limiter.Allow("k", 10, 2, now + std::chrono::seconds(1)));
}

void TestBusinessExecutor() {
    eat::app::BusinessExecutor executor(4, 1024, 3000);
    std::atomic<int> counter{0};
    for (int i = 0; i < 200; ++i) {
        assert(executor.Submit([&counter] { counter.fetch_add(1); }));
    }
    for (int i = 0; i < 100 && counter.load() < 200; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(counter.load() == 200);
    executor.Stop();
}

void TestBusinessExecutorRejects() {
    eat::app::BusinessExecutor executor(1, 2, 3000);
    std::atomic<bool> gate{false};
    assert(executor.Submit([&gate] {
        while (!gate.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(executor.Submit([] {}));
    assert(executor.Submit([] {}));
    assert(!executor.Submit([] {}));
    assert(executor.rejected() == 1);
    gate.store(true);
    executor.Stop();
}

void TestBackpressure() {
    eat::tcp::Backpressure bp(256, 64, 512);
    assert(!bp.ShouldPauseRead(255));
    assert(bp.ShouldPauseRead(256));
    assert(!bp.ShouldResumeRead(65));
    assert(bp.ShouldResumeRead(64));
    assert(bp.OnQueued(400));
    assert(!bp.OnQueued(200));
    bp.OnDrained(400);
    assert(bp.global_queued() == 200);
}

void TestConnContext() {
    eat::tcp::ConnContext context;
    assert(!context.IsAuthenticated());
    context.state = eat::tcp::ConnState::Authenticated;
    assert(context.IsAuthenticated());
    assert(context.Close(eat::protocol::ErrorCode::Replaced));
    assert(!context.Close(eat::protocol::ErrorCode::Replaced));
    assert(context.state == eat::tcp::ConnState::Closed);
}

}  // namespace

int main() {
    TestHeartbeat();
    TestHeartbeatLazyInvalidation();
    TestRateLimiter();
    TestBusinessExecutor();
    TestBusinessExecutorRejects();
    TestBackpressure();
    TestConnContext();
    return 0;
}