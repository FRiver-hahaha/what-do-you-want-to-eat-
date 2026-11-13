#include <glog/logging.h>

#include "Redis.h"

Redis::Redis() {}

Redis::~Redis() {
    while (!m_pool.empty()) {
        redisFree(m_pool.front());
        m_pool.pop();
    }
}

bool Redis::init(int size, const std::string& ip, unsigned short port,
                 const std::string& password) {
    for (int i = 0; i < size; i++) {
        redisContext* conn = redisConnect(ip.c_str(), port);
        if (conn == nullptr || conn->err) {
            if (conn) {
                redisFree(conn);
            }
            return false;
        }
        if (!password.empty()) {
            redisReply* reply =
                (redisReply*)redisCommand(conn, "AUTH %b", password.data(),
                                          password.size());
            bool authed =
                (reply != nullptr && reply->type == REDIS_REPLY_STATUS);
            if (reply != nullptr) {
                freeReplyObject(reply);
            }
            if (!authed) {
                LOG(ERROR) << "Redis AUTH failed: "
                           << (conn->err ? conn->errstr : "wrong password");
                redisFree(conn);
                return false;
            }
        }
        m_pool.push(conn);
    }
    return true;
}

redisContext* Redis::borrow() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return !m_pool.empty(); });
    redisContext* conn = m_pool.front();
    m_pool.pop();
    return conn;
}

void Redis::returnConn(redisContext* conn) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool.push(conn);
    m_cv.notify_one();
}

bool Redis::lpush(const std::string& key, const std::string& value) {
    redisContext* conn = borrow();
    redisReply* reply =
        (redisReply*)redisCommand(conn, "LPUSH %b %b", key.data(), key.size(),
                                  value.data(), value.size());
    bool success = false;
    if (reply == nullptr) {
        LOG(ERROR) << "Redis lpush failed: " << conn->errstr;
    } else {
        success = (reply->type == REDIS_REPLY_INTEGER);
        freeReplyObject(reply);
    }
    returnConn(conn);
    return success;
}

bool Redis::rpop(const std::string& key, std::string& value) {
    redisContext* conn = borrow();
    redisReply* reply = (redisReply*)redisCommand(conn, "RPOP %b", key.data(),
                                                  key.size());
    bool success = false;
    if (reply == nullptr) {
        LOG(ERROR) << "Redis rpop failed: " << conn->errstr;
    } else {
        success = (reply->type == REDIS_REPLY_STRING);
        if (success) {
            value = reply->str;
        }
        freeReplyObject(reply);
    }
    returnConn(conn);
    return success;
}

int Redis::getLen() {
    redisContext* conn = borrow();
    redisReply* reply = (redisReply*)redisCommand(conn, "LLEN msg");
    int len = 0;
    if (reply == nullptr) {
        LOG(ERROR) << "Redis llen failed: " << conn->errstr;
    } else {
        if (reply->type == REDIS_REPLY_INTEGER) {
            len = static_cast<int>(reply->integer);
        }
        freeReplyObject(reply);
    }
    returnConn(conn);
    return len;
}