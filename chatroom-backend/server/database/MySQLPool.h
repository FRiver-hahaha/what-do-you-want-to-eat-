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
    MYSQL* borrow();
    void returnConn(MYSQL* conn);

   private:
    std::queue<MYSQL*> m_pool;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};