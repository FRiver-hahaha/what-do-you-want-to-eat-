#include <atomic>
#include <cassert>
#include <chrono>
#include <stdexcept>
#include <thread>

#include "server/app/ThreadPool.h"

namespace {

void TestExecutesAll() {
    eat::app::ThreadPool pool(4, 256);
    std::atomic<int> counter{0};
    for (int i = 0; i < 200; ++i) {
        assert(pool.Submit([&counter] { counter.fetch_add(1); }));
    }
    for (int i = 0; i < 200 && counter.load() < 200; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(counter.load() == 200);
    assert(pool.executed() == 200);
    pool.Stop();
}

void TestRejectsWhenFull() {
    eat::app::ThreadPool pool(1, 2);
    std::atomic<bool> gate{false};
    assert(pool.Submit([&gate] {
        while (!gate.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(pool.Submit([] {}));
    assert(pool.Submit([] {}));
    assert(!pool.Submit([] {}));
    assert(pool.rejected() == 1);
    gate.store(true);
    pool.Stop();
}

void TestDeadlineDropsExpired() {
    eat::app::ThreadPool pool(1, 8);
    std::atomic<bool> gate{false};
    assert(pool.Submit([&gate] {
        while (!gate.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(pool.SubmitBefore([] { assert(false); },
                             std::chrono::steady_clock::now() - std::chrono::seconds(1)));
    gate.store(true);
    for (int i = 0; i < 100 && pool.timed_out() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(pool.timed_out() == 1);
    assert(pool.executed() == 1);
    pool.Stop();
}

void TestContainsExceptions() {
    eat::app::ThreadPool pool(2, 16);
    assert(pool.Submit([] { throw std::runtime_error("boom"); }));
    std::atomic<int> ok{0};
    assert(pool.Submit([&ok] { ok.fetch_add(1); }));
    for (int i = 0; i < 100 && ok.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(ok.load() == 1);
    assert(pool.failed() == 1);
    pool.Stop();
}

void TestStopRejectsAndDrops() {
    eat::app::ThreadPool pool(1, 8);
    std::atomic<bool> gate{false};
    assert(pool.Submit([&gate] {
        while (!gate.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(pool.Submit([] {}));
    assert(pool.Submit([] {}));
    std::thread releaser([&gate] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        gate.store(true);
    });
    pool.Stop();
    releaser.join();
    assert(pool.dropped() == 2);
    assert(!pool.Submit([] {}));
}

}  // namespace

int main() {
    TestExecutesAll();
    TestRejectsWhenFull();
    TestDeadlineDropsExpired();
    TestContainsExceptions();
    TestStopRejectsAndDrops();
    return 0;
}