#pragma once

#include <string>

#include "http/service/ServiceResult.h"

class MySQLPool;

// 统计：当日营养缺口与周统计（健康分严格复刻前端 getHealthAnalysis）。
class StatsService {
   public:
    explicit StatsService(MySQLPool& pool) : m_pool(pool) {}

    // date 为空时按本地当天。
    ServiceResult nutritionGap(const std::string& agentUid,
                               const std::string& date) const;

    ServiceResult weeklyStats(const std::string& agentUid) const;

   private:
    MySQLPool& m_pool;
};