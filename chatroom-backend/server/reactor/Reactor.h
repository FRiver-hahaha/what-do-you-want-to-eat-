#pragma once

#include <sys/epoll.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "../net/Connection.h"
#include "../service/ChatService.h"

class Connection;

class Reactor {
   public:
    Reactor();
    ~Reactor();
    void loop();
    void pushFd(int fd);
    int getWakeupFd() const;
    int getConnCnt() const;
    void handleWrite(std::shared_ptr<Connection> conn, const std::string& data);
    void enableOut(int fd);
    void disableOut(int fd);
    void post(std::function<void()> task);

   private:
    void handleWakeup();
    void handleClose(std::shared_ptr<Connection> conn);
    void handleRead(std::shared_ptr<Connection> conn);
    void handleHeartbeat();

   private:
    int m_epfd;
    int m_wakeup_fd;
    std::atomic<int> m_conn_cnt{0};
    std::mutex m_mtx;
    std::vector<int> m_cds;
    std::vector<std::function<void()>> m_tasks;
    std::unordered_map<int, std::shared_ptr<Connection>> m_conns;

    static constexpr int MAX_EVENTS = 1024;
    static constexpr int HEARTBEAT_MS = 20000;
    static constexpr int IDLE_TIMEOUT_MS = 60000;
};