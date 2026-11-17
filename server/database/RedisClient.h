#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace eat::db {

struct RedisReply {
    enum class Type { Simple, Error, Integer, Bulk, Array };

    Type type = Type::Error;
    std::string text;
    std::int64_t integer = 0;
    bool nil = false;
    std::vector<RedisReply> array;

    bool ok() const { return type != Type::Error; }
};

// Minimal RESP client with a bounded connection pool. Commands are limited to
// the cache-safe whitelist; pub/sub, SCAN and KEYS are deliberately absent.
class RedisClient {
 public:
    struct Options {
        int pool_size = 8;
        int connect_timeout_ms = 50;
        int io_timeout_ms = 20;
        std::string password;
    };

    struct Stats {
        std::uint64_t commands = 0;
        std::uint64_t failures = 0;
        std::uint64_t reconnects = 0;
    };

    RedisClient();
    ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    bool Init(const std::string& host, std::uint16_t port, const Options& options,
              std::string& error);
    void Shutdown();

    enum class GetResult { Hit, Miss, Error };
    GetResult Get(const std::string& key, std::string& value);
    bool SetEx(const std::string& key, std::uint32_t ttl_s, const std::string& value);
    bool Del(const std::vector<std::string>& keys);
    bool MGet(const std::vector<std::string>& keys, std::vector<std::string>& values);
    bool Exists(const std::string& key);
    bool Ping();

    Stats stats() const;

 private:
    struct Connection;

    bool Command(const std::vector<std::string>& args, RedisReply& reply);
    Connection* Borrow(int timeout_ms);
    void GiveBack(Connection* connection);
    Connection* CreateLocked();

    mutable std::mutex mutex_;
    std::string host_;
    std::uint16_t port_ = 6379;
    Options options_;
    std::vector<Connection*> idle_;
    int live_ = 0;
    bool initialized_ = false;
    bool stopping_ = false;
    mutable Stats stats_;
};

}  // namespace eat::db
