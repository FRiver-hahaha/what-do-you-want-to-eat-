#include "server/app/Config.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace eat::app {
namespace {

std::string Trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

bool ParseU64(const std::string& value, std::uint64_t& out) {
    if (value.empty()) return false;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') return false;
    out = parsed;
    return true;
}

bool Assign(Config& config, const std::string& key, const std::string& value) {
    std::uint64_t number = 0;
    const bool numeric = ParseU64(value, number);
    const auto need_number = [&](std::uint64_t& target) {
        if (!numeric) return false;
        target = number;
        return true;
    };

    if (key == "max_connections") return need_number(config.max_connections);
    if (key == "listen_backlog") { std::uint64_t v; if (!need_number(v)) return false; config.listen_backlog = static_cast<std::uint32_t>(v); return true; }
    if (key == "heartbeat_interval_s") { std::uint64_t v; if (!need_number(v)) return false; config.heartbeat_interval_s = static_cast<std::uint32_t>(v); return true; }
    if (key == "idle_timeout_s") { std::uint64_t v; if (!need_number(v)) return false; config.idle_timeout_s = static_cast<std::uint32_t>(v); return true; }
    if (key == "sub_reactors") { std::uint64_t v; if (!need_number(v)) return false; config.sub_reactors = static_cast<std::uint32_t>(v); return true; }
    if (key == "epoll_batch") { std::uint64_t v; if (!need_number(v)) return false; config.epoll_batch = static_cast<std::uint32_t>(v); return true; }
    if (key == "max_frame_size") return need_number(config.max_frame_size);
    if (key == "max_frame_hard_limit") return need_number(config.max_frame_hard_limit);
    if (key == "read_buffer_limit") return need_number(config.read_buffer_limit);
    if (key == "send_high_watermark") return need_number(config.send_high_watermark);
    if (key == "send_low_watermark") return need_number(config.send_low_watermark);
    if (key == "global_send_high_watermark") return need_number(config.global_send_high_watermark);
    if (key == "slow_consumer_timeout_s") { std::uint64_t v; if (!need_number(v)) return false; config.slow_consumer_timeout_s = static_cast<std::uint32_t>(v); return true; }
    if (key == "business_threads") { std::uint64_t v; if (!need_number(v)) return false; config.business_threads = static_cast<std::uint32_t>(v); return true; }
    if (key == "business_queue_capacity") { std::uint64_t v; if (!need_number(v)) return false; config.business_queue_capacity = static_cast<std::uint32_t>(v); return true; }
    if (key == "task_timeout_ms") { std::uint64_t v; if (!need_number(v)) return false; config.task_timeout_ms = static_cast<std::uint32_t>(v); return true; }
    if (key == "global_inflight_limit") { std::uint64_t v; if (!need_number(v)) return false; config.global_inflight_limit = static_cast<std::uint32_t>(v); return true; }
    if (key == "db_pool_size") { std::uint64_t v; if (!need_number(v)) return false; config.db_pool_size = static_cast<std::uint32_t>(v); return true; }
    if (key == "db_pool_max") { std::uint64_t v; if (!need_number(v)) return false; config.db_pool_max = static_cast<std::uint32_t>(v); return true; }
    if (key == "db_borrow_timeout_ms") { std::uint64_t v; if (!need_number(v)) return false; config.db_borrow_timeout_ms = static_cast<std::uint32_t>(v); return true; }
    if (key == "db_statement_timeout_ms") { std::uint64_t v; if (!need_number(v)) return false; config.db_statement_timeout_ms = static_cast<std::uint32_t>(v); return true; }
    if (key == "rate_per_conn_per_s") { std::uint64_t v; if (!need_number(v)) return false; config.rate_per_conn_per_s = static_cast<std::uint32_t>(v); return true; }
    if (key == "rate_per_conn_burst") { std::uint64_t v; if (!need_number(v)) return false; config.rate_per_conn_burst = static_cast<std::uint32_t>(v); return true; }
    if (key == "rate_per_user_per_s") { std::uint64_t v; if (!need_number(v)) return false; config.rate_per_user_per_s = static_cast<std::uint32_t>(v); return true; }
    if (key == "rate_per_user_burst") { std::uint64_t v; if (!need_number(v)) return false; config.rate_per_user_burst = static_cast<std::uint32_t>(v); return true; }
    if (key == "rate_global_per_s") { std::uint64_t v; if (!need_number(v)) return false; config.rate_global_per_s = static_cast<std::uint32_t>(v); return true; }
    if (key == "handshake_per_ip_per_s") { std::uint64_t v; if (!need_number(v)) return false; config.handshake_per_ip_per_s = static_cast<std::uint32_t>(v); return true; }
    if (key == "handshake_per_ip_burst") { std::uint64_t v; if (!need_number(v)) return false; config.handshake_per_ip_burst = static_cast<std::uint32_t>(v); return true; }
    if (key == "handshake_global_per_s") { std::uint64_t v; if (!need_number(v)) return false; config.handshake_global_per_s = static_cast<std::uint32_t>(v); return true; }
    if (key == "login_per_min") { std::uint64_t v; if (!need_number(v)) return false; config.login_per_min = static_cast<std::uint32_t>(v); return true; }
    if (key == "cache_get_timeout_ms") { std::uint64_t v; if (!need_number(v)) return false; config.cache_get_timeout_ms = static_cast<std::uint32_t>(v); return true; }
    if (key == "cache_connect_timeout_ms") { std::uint64_t v; if (!need_number(v)) return false; config.cache_connect_timeout_ms = static_cast<std::uint32_t>(v); return true; }
    if (key == "cache_breaker_failures") { std::uint64_t v; if (!need_number(v)) return false; config.cache_breaker_failures = static_cast<std::uint32_t>(v); return true; }
    if (key == "cache_breaker_open_ms") { std::uint64_t v; if (!need_number(v)) return false; config.cache_breaker_open_ms = static_cast<std::uint32_t>(v); return true; }
    if (key == "cache_gen") return need_number(config.cache_gen);
    if (key == "tcp_port") { std::uint64_t v; if (!need_number(v)) return false; config.tcp_port = static_cast<std::uint16_t>(v); return true; }
    if (key == "admin_port") { std::uint64_t v; if (!need_number(v)) return false; config.admin_port = static_cast<std::uint16_t>(v); return true; }
    if (key == "mysql_host") { config.mysql_host = value; return true; }
    if (key == "mysql_port") { std::uint64_t v; if (!need_number(v)) return false; config.mysql_port = static_cast<std::uint16_t>(v); return true; }
    if (key == "mysql_db") { config.mysql_db = value; return true; }
    if (key == "mysql_user") { config.mysql_user = value; return true; }
    if (key == "mysql_password") { config.mysql_password = value; return true; }
    if (key == "mysql_socket") { config.mysql_socket = value; return true; }
    if (key == "redis_host") { config.redis_host = value; return true; }
    if (key == "redis_port") { std::uint64_t v; if (!need_number(v)) return false; config.redis_port = static_cast<std::uint16_t>(v); return true; }
    if (key == "redis_password") { config.redis_password = value; return true; }
    return false;
}

// Secrets are never required in the tracked config file: environment variables
// win over file values so production can inject them at runtime.
void ApplyEnvironment(Config& config) {
    if (const char* v = std::getenv("EAT_MYSQL_HOST")) config.mysql_host = v;
    if (const char* v = std::getenv("EAT_MYSQL_PORT")) {
        config.mysql_port = static_cast<std::uint16_t>(std::strtoul(v, nullptr, 10));
    }
    if (const char* v = std::getenv("EAT_MYSQL_DB")) config.mysql_db = v;
    if (const char* v = std::getenv("EAT_MYSQL_USER")) config.mysql_user = v;
    if (const char* v = std::getenv("EAT_MYSQL_PASSWORD")) config.mysql_password = v;
    if (const char* v = std::getenv("EAT_MYSQL_SOCKET")) config.mysql_socket = v;
    if (const char* v = std::getenv("EAT_REDIS_HOST")) config.redis_host = v;
    if (const char* v = std::getenv("EAT_REDIS_PORT")) {
        config.redis_port = static_cast<std::uint16_t>(std::strtoul(v, nullptr, 10));
    }
    if (const char* v = std::getenv("EAT_REDIS_PASSWORD")) config.redis_password = v;
}

}  // namespace

bool Config::LoadFromFile(const std::string& path, std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "config_open_failed";
        return false;
    }
    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '[') {
            continue;
        }
        const auto eq = trimmed.find('=');
        if (eq == std::string::npos) {
            error = "config_bad_line:" + std::to_string(line_number);
            return false;
        }
        const std::string key = Trim(trimmed.substr(0, eq));
        const std::string value = Trim(trimmed.substr(eq + 1));
        if (!Assign(*this, key, value)) {
            error = "config_unknown_or_bad_value:" + key + ":" + std::to_string(line_number);
            return false;
        }
    }
    ApplyEnvironment(*this);
    return Validate(error);
}

bool Config::Validate(std::string& error) const {
    if (heartbeat_interval_s < 60 || heartbeat_interval_s > 120) {
        error = "heartbeat_out_of_range";
        return false;
    }
    if (idle_timeout_s < heartbeat_interval_s * 5 / 2) {
        error = "idle_timeout_too_small";
        return false;
    }
    if (max_frame_size == 0 || max_frame_size > max_frame_hard_limit ||
        max_frame_hard_limit > 262144) {
        error = "max_frame_size_out_of_range";
        return false;
    }
    if (send_low_watermark > send_high_watermark) {
        error = "send_watermark_invalid";
        return false;
    }
    if (business_threads < 8 || business_threads > 64) {
        error = "business_threads_out_of_range";
        return false;
    }
    if (business_queue_capacity < business_threads) {
        error = "business_queue_too_small";
        return false;
    }
    if (db_pool_size == 0 || db_pool_size > db_pool_max || db_pool_max > 64) {
        error = "db_pool_invalid";
        return false;
    }
    return true;
}

}  // namespace eat::app