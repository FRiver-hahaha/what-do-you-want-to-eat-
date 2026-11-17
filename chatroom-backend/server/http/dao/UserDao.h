#pragma once

#include <string>

class MySQLPool;

// agent_uid 是字符串标识，只做等值查询，绝不转换成 MySQL int id。
class UserDao {
   public:
    enum class Lookup { Found, NotFound, Unavailable };

    explicit UserDao(MySQLPool& pool) : m_pool(pool) {}

    Lookup findByAgentUid(const std::string& agentUid) const;

   private:
    MySQLPool& m_pool;
};