#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

#include "TcpClient.h"

TcpClient::~TcpClient() {
    closeFd();
}

bool TcpClient::connectServer(std::string ip, int port) {
    m_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_fd < 0) {
        return false;
    }
    int one = 1;
    setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    sockaddr_in serv{};
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &serv.sin_addr) <= 0) {
        closeFd();
        return false;
    }
    if (connect(m_fd, reinterpret_cast<sockaddr*>(&serv), sizeof(serv)) < 0) {
        closeFd();
        return false;
    }
    int flags = fcntl(m_fd, F_GETFL, 0);
    fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);
    return true;
}

std::string TcpClient::recvData() {
    if (m_fd == -1) {
        return "";
    }
    char buf[65536];
    while (true) {
        ssize_t n = read(m_fd, buf, sizeof(buf));
        if (n > 0) {
            m_buf.append(buf, static_cast<size_t>(n));
        } else if (n == 0) {
            m_closed = true;
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else if (errno == EINTR) {
                continue;
            } else {
                m_closed = true;
                break;
            }
        }
    }
    size_t pos = m_buf.find('\n');
    if (pos == std::string::npos) {
        return "";
    }
    std::string msg = m_buf.substr(0, pos);
    m_buf.erase(0, pos + 1);
    return msg;
}

bool TcpClient::sendData(std::string msg) {
    if (m_fd == -1) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(m_write_mtx);
        m_write_buf.append(msg).append("\n");
    }
    return flush();
}

bool TcpClient::flush() {
    std::lock_guard<std::mutex> lk(m_write_mtx);
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

void TcpClient::closeFd() {
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
}

int TcpClient::getFd() const {
    return m_fd;
}