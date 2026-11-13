#pragma once

#include <hiredis/hiredis.h>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>

class Redis {
   public:
    Redis();
    ~Redis();
    bool init(int size, const std::string& ip, unsigned short port = 6379,
              const std::string& password = "");
    bool lpush(const std::string& key, const std::string& value);
    bool rpop(const std::string& key, std::string& value);
    int getLen();

   private:
    redisContext* borrow();
    void returnConn(redisContext* conn);

    std::queue<redisContext*> m_pool;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};