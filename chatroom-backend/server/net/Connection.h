#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "../reactor/Reactor.h"

class Reactor;

class Connection {
   public:
    explicit Connection(int fd, Reactor* reactor);
    ~Connection();
    int getFd() const;
    bool recvData();
    bool getMessage(std::string& msg);
    bool sendData(const std::string& data);
    bool flush();
    void closeFd();
    bool isClosed() const;
    void setUserId(int id);
    int getUserId() const;
    Reactor* getReactor() const;
    void updateActive();
    bool isIdle(int ms) const;
    static constexpr size_t kMaxBufSize = 64 * 1024;  // 单连接读缓冲上限

   private:
    int m_fd{-1};
    int m_user_id{-1};
    Reactor* m_reactor;
    std::string m_read_buf;
    std::string m_write_buf;
    std::chrono::steady_clock::time_point m_last_active{
        std::chrono::steady_clock::now()};
};