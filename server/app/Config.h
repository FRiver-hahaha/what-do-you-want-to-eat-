#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace eat::app {

struct Config {
    std::uint64_t max_connections = 1000000;
    std::uint32_t listen_backlog = 65535;
    std::uint32_t heartbeat_interval_s = 60;
    std::uint32_t idle_timeout_s = 165;
    std::uint32_t sub_reactors = 16;
    std::uint32_t epoll_batch = 2048;

    std::size_t max_frame_size = 65536;
    std::size_t max_frame_hard_limit = 262144;
    std::size_t read_buffer_limit = 65576;
    std::size_t send_high_watermark = 262144;
    std::size_t send_low_watermark = 65536;
    std::size_t global_send_high_watermark = 536870912;
    std::uint32_t slow_consumer_timeout_s = 30;

    std::uint32_t business_threads = 16;
    std::uint32_t business_queue_capacity = 8192;
    std::uint32_t task_timeout_ms = 3000;
    std::uint32_t global_inflight_limit = 1024;

    std::uint32_t db_pool_size = 32;
    std::uint32_t db_pool_max = 64;
    std::uint32_t db_borrow_timeout_ms = 200;
    std::uint32_t db_statement_timeout_ms = 2500;

    std::uint32_t rate_per_conn_per_s = 60;
    std::uint32_t rate_per_conn_burst = 120;
    std::uint32_t rate_per_user_per_s = 120;
    std::uint32_t rate_per_user_burst = 240;
    std::uint32_t rate_global_per_s = 5000;
    std::uint32_t handshake_per_ip_per_s = 5;
    std::uint32_t handshake_per_ip_burst = 10;
    std::uint32_t handshake_global_per_s = 3000;
    std::uint32_t login_per_min = 5;

    std::uint32_t cache_get_timeout_ms = 20;
    std::uint32_t cache_connect_timeout_ms = 50;
    std::uint32_t cache_breaker_failures = 3;
    std::uint32_t cache_breaker_open_ms = 5000;
    std::uint64_t cache_gen = 1;

    std::uint16_t tcp_port = 9443;
    std::uint16_t admin_port = 9080;
    std::string mysql_host = "127.0.0.1";
    std::uint16_t mysql_port = 3306;
    std::string mysql_db = "eat";
    std::string mysql_user = "eat";
    std::string redis_host = "127.0.0.1";
    std::uint16_t redis_port = 6379;

    bool LoadFromFile(const std::string& path, std::string& error);
    bool Validate(std::string& error) const;
};

}  // namespace eat::app