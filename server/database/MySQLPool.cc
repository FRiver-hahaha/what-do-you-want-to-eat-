#include "server/database/MySQLPool.h"

#include <chrono>
#include <utility>

namespace eat::db {
namespace {

constexpr unsigned int kConnectTimeoutSeconds = 3;

MYSQL* OpenConnection(const std::string& host, std::uint16_t port,
                      const std::string& user, const std::string& password,
                      const std::string& database, const std::string& socket) {
    MYSQL* connection = mysql_init(nullptr);
    if (connection == nullptr) return nullptr;
    unsigned int timeout = kConnectTimeoutSeconds;
    mysql_options(connection, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(connection, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    const char* socket_arg = socket.empty() ? nullptr : socket.c_str();
    if (mysql_real_connect(connection, host.c_str(), user.c_str(), password.c_str(),
                           database.c_str(), static_cast<unsigned int>(port),
                           socket_arg, 0) == nullptr) {
        mysql_close(connection);
        return nullptr;
    }
    mysql_set_character_set(connection, "utf8mb4");
    return connection;
}

}  // namespace

MySQLPool::MySQLPool() = default;

MySQLPool::~MySQLPool() { Shutdown(); }

bool MySQLPool::Init(int size, const std::string& host, std::uint16_t port,
                     const std::string& user, const std::string& password,
                     const std::string& database, const std::string& socket,
                     std::string& error) {
    if (size <= 0) {
        error = "db_pool_size_invalid";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    host_ = host;
    port_ = port;
    user_ = user;
    password_ = password;
    database_ = database;
    socket_ = socket;
    capacity_ = size;

    for (int i = 0; i < size; ++i) {
        MYSQL* connection = OpenConnection(host_, port_, user_, password_, database_, socket_);
        if (connection == nullptr) {
            while (!pool_.empty()) {
                mysql_close(pool_.front());
                pool_.pop();
            }
            live_ = 0;
            capacity_ = 0;
            error = "db_connect_failed";
            return false;
        }
        pool_.push(connection);
        ++live_;
    }
    return true;
}

MYSQL* MySQLPool::CreateConnection() {
    return OpenConnection(host_, port_, user_, password_, database_, socket_);
}

MYSQL* MySQLPool::AcquireLocked() {
    if (pool_.empty()) {
        if (live_ >= capacity_) return nullptr;
        MYSQL* created = CreateConnection();
        if (created == nullptr) return nullptr;
        ++live_;
        ++stats_.reconnects;
        return created;
    }
    MYSQL* connection = pool_.front();
    pool_.pop();
    if (mysql_ping(connection) != 0) {
        mysql_close(connection);
        --live_;
        connection = CreateConnection();
        if (connection == nullptr) return nullptr;
        ++live_;
        ++stats_.reconnects;
    }
    return connection;
}

MYSQL* MySQLPool::Borrow(int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (stopping_) return nullptr;
    ++stats_.borrowed;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        MYSQL* connection = AcquireLocked();
        if (connection != nullptr) return connection;
        if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
            ++stats_.timeouts;
            return nullptr;
        }
        if (stopping_) return nullptr;
    }
}

void MySQLPool::Return(MYSQL* connection) {
    if (connection == nullptr) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            mysql_close(connection);
            --live_;
            return;
        }
        ++stats_.returned;
        pool_.push(connection);
    }
    cv_.notify_one();
}

void MySQLPool::Shutdown() {
    std::queue<MYSQL*> drained;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ && pool_.empty()) return;
        stopping_ = true;
        drained.swap(pool_);
        live_ -= static_cast<int>(drained.size());
        capacity_ = 0;
    }
    cv_.notify_all();
    while (!drained.empty()) {
        mysql_close(drained.front());
        drained.pop();
    }
}

MySQLPool::Stats MySQLPool::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

}  // namespace eat::db
