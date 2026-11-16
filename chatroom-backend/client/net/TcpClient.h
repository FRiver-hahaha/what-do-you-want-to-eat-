#pragma once

#include <mutex>
#include <string>

class TcpClient {
   public:
    ~TcpClient();

    bool connectServer(std::string ip, int port);
    std::string recvData();
    bool sendData(std::string msg);
    bool flush();
    void closeFd();
    int getFd() const;
    bool isClosed() const { return m_closed; }

   private:
    int m_fd{-1};
    std::string m_buf;
    std::string m_write_buf;
    std::mutex m_write_mtx;
    bool m_closed{false};
};