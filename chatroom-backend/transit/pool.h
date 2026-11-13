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
                            // 移动赋值就是为了消除不必要的数据拷贝开销，提高性能
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
        // 使用 packaged_task 封装任务并绑定返回值
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            // 完美转发函数和参数，保证参数的类型和值类别（左值或右值）不变。
            [func = std::forward<F>(f),
             args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                // std::apply将元组（tuple）展开为参数，调用函数。
                return std::apply(func, args);
            });
        // 获取 future 对象
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(mtx);
            // 将任务包装为std::function<void()>类型
            works.emplace([task]() { (*task)(); });
        }
        cv.notify_one();
        return res;
    }
};