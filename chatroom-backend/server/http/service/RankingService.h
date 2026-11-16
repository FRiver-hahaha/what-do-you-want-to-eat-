#pragma once

#include <string>

#include "http/service/ServiceResult.h"

class MySQLPool;

// 好友健康分排行榜：只统计自己与 status=2 的正常好友，
// 评分口径复用 HealthScore（与 S2 周统计一致）。
class RankingService {
   public:
    explicit RankingService(MySQLPool& pool) : m_pool(pool) {}

    // scope: week | today
    ServiceResult friendRanking(const std::string& agentUid,
                                const std::string& scope) const;

   private:
    bool scoreFor(const std::string& agentUid, const std::string& scope,
                  double& out) const;

    MySQLPool& m_pool;
};