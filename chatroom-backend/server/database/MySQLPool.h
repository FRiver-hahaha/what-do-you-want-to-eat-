#pragma once

#include <mysql/mysql.h>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>

class MySQLPool {
   public:
    MySQLPool();
    ~MySQLPool();

    bool init(int size,
              const std::string& user,
              const std::string& passwd,
              const std::string& db,
              const std::string& ip,
              unsigned short port = 3306);

    // 兼容 P7 TCP 进程：池空时无限等待。
    MYSQL* borrow();
    // HTTP 进程专用：限时借用，超时返回 nullptr，绝不永久阻塞工作线程。
    MYSQL* borrow_for(int timeoutMs);
    void returnConn(MYSQL* conn);

    int capacity() const { return m_capacity; }

   private:
    MYSQL* createConn();

    std::queue<MYSQL*> m_pool;
    std::mutex m_mutex;
    std::condition_variable m_cv;

    std::string m_user;
    std::string m_passwd;
    std::string m_db;
    std::string m_ip;
    unsigned short m_port = 3306;
    int m_capacity = 0;
    // 存活连接数（池内 + 已借出）。断连丢弃时递减，补建时先占名额，
    // 保证池被耗尽后仍可自愈且不会超过容量。
    int m_live = 0;
};