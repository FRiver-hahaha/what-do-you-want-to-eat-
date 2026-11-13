#pragma once
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class pool {
   private:
    int thread_count;
    std::queue<std::function<void()>> works;
    std::vector<std::thread> thd;
    std::mutex mtx;
    std::condition_variable cv;
    bool runflag = true;

   public:
    pool(int n = 4) : thread_count(n) {
        for (int i = 0; i < thread_count; ++i) {
            thd.emplace_back([this]() {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mtx);
                        cv.wait(lock,
                                [this] { return !works.empty() || !runflag; });
                        if (!runflag && works.empty())
                            return;
                        if (!works.empty()) {
                            task = std::move(works.front());
                            works.pop();
                        }
                    }
                    if (task)
                        task();
                }
            });
        }
    }

    ~pool() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            runflag = false;
        }
        cv.notify_all();
        for (auto& t : thd) {
            if (t.joinable())
                t.join();
        }
    }

   public:
    template <typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            [func = std::forward<F>(f),
             args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                return std::apply(func, args);
            });
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(mtx);
            works.emplace([task]() { (*task)(); });
        }
        cv.notify_one();
        return res;
    }
};