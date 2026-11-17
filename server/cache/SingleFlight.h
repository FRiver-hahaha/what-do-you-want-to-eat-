#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace eat::cache {

class SingleFlight {
 public:
    using Loader = std::function<std::string()>;

    std::string Do(const std::string& key, const Loader& loader,
                   std::chrono::milliseconds wait);
    std::size_t in_flight() const;

 private:
    struct State {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        std::string value;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<State>> states_;
};

}  // namespace eat::cache