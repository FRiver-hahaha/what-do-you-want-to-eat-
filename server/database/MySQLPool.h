#pragma once

#include <mysql/mysql.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>

namespace eat::db {

// Bounded MySQL connection pool. Borrow() never blocks forever: it waits up to
// the caller-provided timeout and then returns nullptr so the business thread
// can answer 5001 service_unavailable instead of stalling the reactor.
// Broken connections are detected with mysql_ping and replaced on the fly.
class MySQLPool {
 public:
    struct Stats {
        std::uint64_t borrowed = 0;
        std::uint64_t returned = 0;
        std::uint64_t timeouts = 0;
        std::uint64_t reconnects = 0;
    };

    MySQLPool();
    ~MySQLPool();

    MySQLPool(const MySQLPool&) = delete;
    MySQLPool& operator=(const MySQLPool&) = delete;

    bool Init(int size, const std::string& host, std::uint16_t port,
              const std::string& user, const std::string& password,
              const std::string& database, const std::string& socket,
              std::string& error);
    MYSQL* Borrow(int timeout_ms);
    void Return(MYSQL* connection);
    void Shutdown();

    int capacity() const { return capacity_; }
    Stats stats() const;

 private:
    MYSQL* CreateConnection();
    MYSQL* AcquireLocked();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<MYSQL*> pool_;
    int capacity_ = 0;
    int live_ = 0;
    bool stopping_ = false;
    std::string host_;
    std::uint16_t port_ = 3306;
    std::string user_;
    std::string password_;
    std::string database_;
    std::string socket_;
    mutable Stats stats_;
};

}  // namespace eat::db