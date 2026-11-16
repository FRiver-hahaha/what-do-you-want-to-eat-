#include <fcntl.h>
#include <glog/logging.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include "Reactor.h"

Reactor::Reactor() {
    m_epfd = epoll_create1(0);
    m_wakeup_fd = eventfd(0, EFD_NONBLOCK);
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = &m_wakeup_fd;
    epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_wakeup_fd, &ev);
}

Reactor ::~Reactor() {
    close(m_wakeup_fd);
    close(m_epfd);
}

void Reactor ::loop() {
    epoll_event events[MAX_EVENTS];
    auto last_beat = std::chrono::steady_clock::now();
    while (true) {
        int n = epoll_wait(m_epfd, events, MAX_EVENTS, HEARTBEAT_MS);
        auto now = std::chrono::steady_clock::now();
        if (now - last_beat >= std::chrono::milliseconds(HEARTBEAT_MS)) {
            handleHeartbeat();
            last_beat = now;
        }
        for (int i = 0; i < n; i++) {
            if (events[i].data.ptr == &m_wakeup_fd) {
                handleWakeup();
                continue;
            }
            Connection* raw = static_cast<Connection*>(events[i].data.ptr);
            int fd = raw->getFd();
            auto it = m_conns.find(fd);
            if (it == m_conns.end()) {
                continue;
            }
            auto conn = it->second;
            if (events[i].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
                handleClose(conn);
                continue;
            }
            if (events[i].events & EPOLLIN) {
                handleRead(conn);
            }
            if ((events[i].events & EPOLLOUT) && m_conns.count(fd)) {
                if (conn->flush()) {
                    disableOut(fd);
                }
            }
        }
    }
}

void Reactor ::pushFd(int fd) {
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_cds.push_back(fd);
    }
    uint64_t u = 1;
    write(m_wakeup_fd, &u, sizeof(u));
}

void Reactor ::post(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_tasks.push_back(std::move(task));
    }
    uint64_t u = 1;
    write(m_wakeup_fd, &u, sizeof(u));
}

int Reactor ::getWakeupFd() const {
    return m_wakeup_fd;
}

int Reactor ::getConnCnt() const {
    return m_conn_cnt;
}

void Reactor ::handleWakeup() {
    uint64_t tmp;
    while (read(m_wakeup_fd, &tmp, sizeof(tmp)) > 0)
        ;
    std::vector<int> cfds;
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        cfds.swap(m_cds);
        tasks.swap(m_tasks);
    }
    epoll_event ev{};
    for (const auto& fd : cfds) {
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        auto conn = std::make_shared<Connection>(fd, this);
        ev.data.ptr = conn.get();
        if (epoll_ctl(m_epfd, EPOLL_CTL_ADD, fd, &ev) == 0) {
            m_conns[fd] = conn;
            m_conn_cnt++;
            LOG(INFO) << "New connection fd=" << fd;
        }
    }
    for (auto& task : tasks) {
        task();
    }
}

void Reactor ::handleClose(std::shared_ptr<Connection> conn) {
    int fd = conn->getFd();
    int uid = conn->getUserId();
    epoll_ctl(m_epfd, EPOLL_CTL_DEL, fd, nullptr);
    ChatService::instance().logout(conn);
    conn->setUserId(-1);
    m_conns.erase(fd);
    m_conn_cnt--;
    LOG(INFO) << "Connection closed fd=" << fd << " uid=" << uid;
}

void Reactor ::handleRead(std::shared_ptr<Connection> conn) {
    if (!conn->recvData()) {
        handleClose(conn);
        return;
    }
    conn->updateActive();
    std::string msg;
    while (conn->getMessage(msg)) {
        conn->updateActive();
        try {
            auto js = json::parse(msg);
            ChatService::instance().handle(conn, js);
        } catch (const json::exception& e) {
            LOG(ERROR) << "JSON error fd=" << conn->getFd()
                       << " what=" << e.what();
            handleClose(conn);
            return;
        }
    }
}

void Reactor ::handleWrite(std::shared_ptr<Connection> conn,
                           const std::string& data) {
    if (data.empty()) {
        return;
    }
    conn->sendData(data + '\n');
}

void Reactor ::enableOut(int fd) {
    auto it = m_conns.find(fd);
    if (it == m_conns.end()) {
        return;
    }
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLOUT;
    ev.data.ptr = it->second.get();
    epoll_ctl(m_epfd, EPOLL_CTL_MOD, fd, &ev);
}

void Reactor ::disableOut(int fd) {
    auto it = m_conns.find(fd);
    if (it == m_conns.end()) {
        return;
    }
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    ev.data.ptr = it->second.get();
    epoll_ctl(m_epfd, EPOLL_CTL_MOD, fd, &ev);
}

void Reactor ::handleHeartbeat() {
    std::vector<std::shared_ptr<Connection>> idle_conns;
    for (auto& pair : m_conns) {
        if (pair.second->isIdle(IDLE_TIMEOUT_MS)) {
            idle_conns.push_back(pair.second);
        }
    }
    for (auto& conn : idle_conns) {
        LOG(WARNING) << "Heartbeat timeout fd=" << conn->getFd()
                     << " uid=" << conn->getUserId();
        handleClose(conn);
    }
}