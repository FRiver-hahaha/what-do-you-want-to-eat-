#include "TcpServer.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

TcpServer::TcpServer(uint16_t port) : m_port(port), m_lfd(-1) {}

TcpServer::~TcpServer() {
    closeLfd();
}

bool TcpServer::start() {
    m_lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_lfd < 0) {
        return false;
    }
    fcntl(m_lfd, F_SETFL, fcntl(m_lfd, F_GETFL, 0) | O_NONBLOCK);
    int opt = 1;
    if (setsockopt(m_lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        closeLfd();
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(m_lfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        closeLfd();
        return false;
    }
    if (listen(m_lfd, BACKLOG) < 0) {
        closeLfd();
        return false;
    }
    return true;
}

int TcpServer::acceptCli() {
    sockaddr_in cli{};
    socklen_t len = sizeof(cli);
    return accept(m_lfd, (sockaddr*)&cli, &len);
}

int TcpServer::getLfd() const {
    return m_lfd;
}

void TcpServer::closeLfd() {
    if (m_lfd != -1) {
        close(m_lfd);
        m_lfd = -1;
    }
}