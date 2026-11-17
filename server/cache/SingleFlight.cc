#include "server/cache/SingleFlight.h"

namespace eat::cache {

std::string SingleFlight::Do(const std::string& key, const Loader& loader,
                             std::chrono::milliseconds wait) {
    std::shared_ptr<State> state;
    bool leader = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = states_.find(key);
        if (it == states_.end()) {
            state = std::make_shared<State>();
            states_[key] = state;
            leader = true;
        } else {
            state = it->second;
        }
    }

    if (leader) {
        const std::string value = loader();
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->value = value;
            state->done = true;
        }
        state->cv.notify_all();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            states_.erase(key);
        }
        return value;
    }

    std::unique_lock<std::mutex> lock(state->mutex);
    if (state->cv.wait_for(lock, wait, [&state] { return state->done; })) {
        return state->value;
    }
    lock.unlock();
    return loader();
}

std::size_t SingleFlight::in_flight() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return states_.size();
}

}  // namespace eat::cache