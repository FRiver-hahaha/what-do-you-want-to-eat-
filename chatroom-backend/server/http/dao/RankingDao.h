#pragma once

#include <string>
#include <vector>

class MySQLPool;

struct RankingMember {
    std::string agentUid;
    std::string name;
};

class RankingDao {
   public:
    enum class Lookup { Found, Unavailable };

    explicit RankingDao(MySQLPool& pool) : m_pool(pool) {}

    // 好友关系严格取 friend.status=2（正常好友），按正/反两个方向合并去重。
    // 不含自己，不返回 status=0/1/3 的关系，避免把非好友用户带进排行榜。
    Lookup listFriends(const std::string& agentUid,
                       std::vector<RankingMember>& out) const;

   private:
    MySQLPool& m_pool;
};