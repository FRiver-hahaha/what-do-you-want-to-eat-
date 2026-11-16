#include <unistd.h>
#include <cerrno>

#include "Connection.h"

Connection::Connection(int fd, Reactor* reactor)
    : m_fd(fd), m_reactor(reactor) {}

Connection::~Connection() {
    closeFd();
}

int Connection::getFd() const {
    return m_fd;
}

bool Connection::recvData() {
    if (m_fd == -1) {
        return false;
    }
    char buf[65536];
    while (true) {
        ssize_t n = read(m_fd, buf, sizeof(buf));
        if (n > 0) {
            m_read_buf.append(buf, static_cast<size_t>(n));
            if (m_read_buf.size() > kMaxBufSize) {
                return false;  // 超长/畸形报文, 当作连接异常关闭
            }
        } else if (n == 0) {
            return false;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            } else if (errno == EINTR) {
                continue;
            }else {
                return false;
            }
        }
    }
    return true;
}

bool Connection::getMessage(std::string& msg) {
    size_t pos = m_read_buf.find('\n');
    if (pos == std::string::npos) {
        return false;
    }
    msg = m_read_buf.substr(0, pos);
    m_read_buf.erase(0, pos + 1);
    return true;
}

bool Connection::sendData(const std::string& data) {
    if (m_fd == -1) {
        return false;
    }
    m_write_buf.append(data);
    if (!flush()) {
        m_reactor->enableOut(m_fd);
    }
    return true;
}

bool Connection::flush() {
    while (!m_write_buf.empty()) {
        ssize_t n = write(m_fd, m_write_buf.data(), m_write_buf.size());
        if (n > 0) {
            m_write_buf.erase(0, static_cast<size_t>(n));
        } else if (n == 0) {
            return false;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return false;
            } else if (errno == EINTR) {
                continue;
            } else {
                return false;
            }
        }
    }
    return true;
}

void Connection::closeFd() {
    if (m_fd != -1) {
        close(m_fd);
        m_fd = -1;
    }
}

bool Connection::isClosed() const {
    return m_fd == -1;
}

void Connection::setUserId(int id) {
    m_user_id = id;
}

int Connection::getUserId() const {
    return m_user_id;
}

Reactor* Connection::getReactor() const {
    return m_reactor;
}

void Connection::updateActive() {
    m_last_active = std::chrono::steady_clock::now();
}

bool Connection::isIdle(int ms) const {
    auto elapsed = std::chrono::steady_clock::now() - m_last_active;
    return elapsed > std::chrono::milliseconds(ms);
}