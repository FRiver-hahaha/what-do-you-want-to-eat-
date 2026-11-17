#include "server/database/RedisClient.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace eat::db {
namespace {

std::string FormatCommand(const std::vector<std::string>& args) {
    std::string out = "*" + std::to_string(args.size()) + "\r\n";
    for (const auto& arg : args) {
        out += "$" + std::to_string(arg.size()) + "\r\n";
        out += arg;
        out += "\r\n";
    }
    return out;
}

bool ReadLine(int fd, std::string& buffer, std::string& line) {
    for (;;) {
        const auto pos = buffer.find("\r\n");
        if (pos != std::string::npos) {
            line = buffer.substr(0, pos);
            buffer.erase(0, pos + 2);
            return true;
        }
        char chunk[512];
        const ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) return false;
        buffer.append(chunk, static_cast<std::size_t>(n));
        if (buffer.size() > 65536) return false;
    }
}

bool ReadExact(int fd, std::string& buffer, std::size_t size, std::string& out) {
    while (buffer.size() < size + 2) {
        char chunk[512];
        const ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) return false;
        buffer.append(chunk, static_cast<std::size_t>(n));
    }
    out = buffer.substr(0, size);
    buffer.erase(0, size + 2);
    return true;
}

bool ParseReply(int fd, std::string& buffer, RedisReply& out) {
    std::string line;
    if (!ReadLine(fd, buffer, line) || line.empty()) return false;
    const char kind = line[0];
    const std::string payload = line.substr(1);
    switch (kind) {
        case '+':
            out.type = RedisReply::Type::Simple;
            out.text = payload;
            return true;
        case '-':
            out.type = RedisReply::Type::Error;
            out.text = payload;
            return true;
        case ':':
            out.type = RedisReply::Type::Integer;
            out.integer = std::strtoll(payload.c_str(), nullptr, 10);
            return true;
        case '$': {
            out.type = RedisReply::Type::Bulk;
            const long long length = std::strtoll(payload.c_str(), nullptr, 10);
            if (length < 0) {
                out.nil = true;
                return true;
            }
            return ReadExact(fd, buffer, static_cast<std::size_t>(length), out.text);
        }
        case '*': {
            out.type = RedisReply::Type::Array;
            const long long count = std::strtoll(payload.c_str(), nullptr, 10);
            if (count < 0) {
                out.nil = true;
                return true;
            }
            out.array.reserve(static_cast<std::size_t>(count));
            for (long long i = 0; i < count; ++i) {
                RedisReply item;
                if (!ParseReply(fd, buffer, item)) return false;
                out.array.push_back(std::move(item));
            }
            return true;
        }
        default:
            return false;
    }
}

int ConnectWithTimeout(const std::string& host, std::uint16_t port, int timeout_ms) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    const std::string port_text = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result) != 0) return -1;

    int fd = -1;
    for (addrinfo* it = result; it != nullptr; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype | SOCK_CLOEXEC, it->ai_protocol);
        if (fd < 0) continue;
        const int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int status = connect(fd, it->ai_addr, it->ai_addrlen);
        if (status != 0 && errno == EINPROGRESS) {
            pollfd entry{fd, POLLOUT, 0};
            status = poll(&entry, 1, timeout_ms) == 1 ? 0 : -1;
            if (status == 0) {
                int error = 0;
                socklen_t length = sizeof(error);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length);
                if (error != 0) status = -1;
            }
        }
        if (status == 0) {
            fcntl(fd, F_SETFL, flags);
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(result);
    return fd;
}

void SetTimeouts(int fd, int timeout_ms) {
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

}  // namespace

struct RedisClient::Connection {
    int fd = -1;
    std::string buffer;
};

RedisClient::RedisClient() = default;

RedisClient::~RedisClient() { Shutdown(); }

bool RedisClient::Init(const std::string& host, std::uint16_t port,
                       const Options& options, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    host_ = host;
    port_ = port;
    options_ = options;
    if (options_.pool_size < 1) options_.pool_size = 1;
    if (options_.io_timeout_ms < 1) options_.io_timeout_ms = 1;
    initialized_ = true;

    for (int i = 0; i < options_.pool_size; ++i) {
        Connection* connection = CreateLocked();
        if (connection == nullptr) {
            error = "redis_connect_failed";
            initialized_ = true;
            return false;
        }
        idle_.push_back(connection);
    }
    return true;
}

void RedisClient::Shutdown() {
    std::vector<Connection*> drained;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        drained.swap(idle_);
        live_ -= static_cast<int>(drained.size());
        initialized_ = false;
    }
    for (Connection* connection : drained) {
        if (connection->fd >= 0) close(connection->fd);
        delete connection;
    }
}

RedisClient::Connection* RedisClient::CreateLocked() {
    const int fd = ConnectWithTimeout(host_, port_, options_.connect_timeout_ms);
    if (fd < 0) return nullptr;
    SetTimeouts(fd, options_.io_timeout_ms);
    auto* connection = new Connection();
    connection->fd = fd;
    if (!options_.password.empty()) {
        const std::string payload = FormatCommand({"AUTH", options_.password});
        if (send(fd, payload.data(), payload.size(), MSG_NOSIGNAL) <= 0) {
            close(fd);
            delete connection;
            return nullptr;
        }
        RedisReply reply;
        if (!ParseReply(fd, connection->buffer, reply) || !reply.ok()) {
            close(fd);
            delete connection;
            return nullptr;
        }
    }
    ++live_;
    return connection;
}

RedisClient::Connection* RedisClient::Borrow(int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || stopping_) return nullptr;
    if (!idle_.empty()) {
        Connection* connection = idle_.back();
        idle_.pop_back();
        return connection;
    }
    if (live_ < options_.pool_size) {
        Connection* created = CreateLocked();
        if (created != nullptr) ++stats_.reconnects;
        return created;
    }
    (void)timeout_ms;
    return nullptr;
}

void RedisClient::GiveBack(Connection* connection) {
    if (connection == nullptr) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) {
        if (connection->fd >= 0) close(connection->fd);
        --live_;
        delete connection;
        return;
    }
    idle_.push_back(connection);
}

bool RedisClient::Command(const std::vector<std::string>& args, RedisReply& reply) {
    Connection* connection = Borrow(options_.io_timeout_ms);
    if (connection == nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.failures;
        return false;
    }

    const std::string payload = FormatCommand(args);
    bool ok = send(connection->fd, payload.data(), payload.size(), MSG_NOSIGNAL) > 0;
    if (ok) ok = ParseReply(connection->fd, connection->buffer, reply);
    if (!ok || !reply.ok()) {
        close(connection->fd);
        connection->fd = -1;
        std::lock_guard<std::mutex> lock(mutex_);
        --live_;
        delete connection;
        ++stats_.failures;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.commands;
    }
    GiveBack(connection);
    return true;
}

RedisClient::GetResult RedisClient::Get(const std::string& key, std::string& value) {
    RedisReply reply;
    if (!Command({"GET", key}, reply)) return GetResult::Error;
    if (reply.type != RedisReply::Type::Bulk) return GetResult::Error;
    if (reply.nil) {
        value.clear();
        return GetResult::Miss;
    }
    value = reply.text;
    return GetResult::Hit;
}

bool RedisClient::SetEx(const std::string& key, std::uint32_t ttl_s,
                        const std::string& value) {
    RedisReply reply;
    if (!Command({"SETEX", key, std::to_string(ttl_s), value}, reply)) return false;
    return reply.type == RedisReply::Type::Simple;
}

bool RedisClient::Del(const std::vector<std::string>& keys) {
    if (keys.empty()) return true;
    std::vector<std::string> args = {"DEL"};
    args.insert(args.end(), keys.begin(), keys.end());
    RedisReply reply;
    if (!Command(args, reply)) return false;
    return reply.type == RedisReply::Type::Integer;
}

bool RedisClient::MGet(const std::vector<std::string>& keys,
                       std::vector<std::string>& values) {
    values.clear();
    if (keys.empty()) return true;
    std::vector<std::string> args = {"MGET"};
    args.insert(args.end(), keys.begin(), keys.end());
    RedisReply reply;
    if (!Command(args, reply)) return false;
    if (reply.type != RedisReply::Type::Array) return false;
    for (const auto& item : reply.array) {
        values.push_back(item.nil ? std::string() : item.text);
    }
    return true;
}

bool RedisClient::Exists(const std::string& key) {
    RedisReply reply;
    if (!Command({"EXISTS", key}, reply)) return false;
    return reply.type == RedisReply::Type::Integer && reply.integer > 0;
}

bool RedisClient::Ping() {
    RedisReply reply;
    if (!Command({"PING"}, reply)) return false;
    return reply.type == RedisReply::Type::Simple && reply.text == "PONG";
}

RedisClient::Stats RedisClient::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

}  // namespace eat::db
