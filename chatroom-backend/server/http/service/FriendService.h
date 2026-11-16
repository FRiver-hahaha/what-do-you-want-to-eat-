#pragma once

#include <string>

#include "http/service/ServiceResult.h"

class MySQLPool;

// 好友列表：只返回 friend.status=2 的正常好友，正/反两个方向合并去重，
// 展示名回退规则与排行榜完全一致（health_profile.name → user.name）。
class FriendService {
   public:
    explicit FriendService(MySQLPool& pool) : m_pool(pool) {}

    ServiceResult friends(const std::string& agentUid) const;

   private:
    MySQLPool& m_pool;
};