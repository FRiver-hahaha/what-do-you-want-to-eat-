#pragma once
#include <cstdint>

class TcpServer {
   public:
    explicit TcpServer(uint16_t port);
    ~TcpServer();
    bool start();
    int acceptCli();
    int getLfd() const;
    void closeLfd();

   private:
    int m_lfd;
    uint16_t m_port;
    static constexpr int BACKLOG = 1024;
};