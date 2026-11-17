#include <cassert>
#include <string>

#include "server/app/Config.h"

namespace {

void TestExampleConfig() {
    eat::app::Config config;
    std::string error;
    assert(config.LoadFromFile("deploy/server.toml.example", error));
    assert(error.empty());
    assert(config.heartbeat_interval_s == 60);
    assert(config.idle_timeout_s >= 150);
    assert(config.max_frame_size == 65536);
    assert(config.db_pool_size <= config.db_pool_max);
}

void TestBadHeartbeat() {
    eat::app::Config config;
    std::string error;
    config.heartbeat_interval_s = 30;
    config.idle_timeout_s = 165;
    assert(!config.Validate(error));
    assert(error == "heartbeat_out_of_range");
}

void TestIdleTooSmall() {
    eat::app::Config config;
    std::string error;
    config.heartbeat_interval_s = 60;
    config.idle_timeout_s = 100;
    assert(!config.Validate(error));
    assert(error == "idle_timeout_too_small");
}

void TestFrameSizeTooLarge() {
    eat::app::Config config;
    std::string error;
    config.max_frame_size = 300000;
    config.max_frame_hard_limit = 300000;
    assert(!config.Validate(error));
    assert(error == "max_frame_size_out_of_range");
}

}  // namespace

int main() {
    TestExampleConfig();
    TestBadHeartbeat();
    TestIdleTooSmall();
    TestFrameSizeTooLarge();
    return 0;
}