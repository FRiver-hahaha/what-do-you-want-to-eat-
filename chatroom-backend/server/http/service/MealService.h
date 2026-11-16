#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "http/service/ServiceResult.h"

class MySQLPool;

// 饮食记录：写入（含幂等与事务）与按时间窗查询。
class MealService {
   public:
    explicit MealService(MySQLPool& pool) : m_pool(pool) {}

    // body 为 Agent 的 record_meal 请求体（payload + idempotency_key）。
    ServiceResult recordMeal(const std::string& agentUid,
                             const nlohmann::json& body) const;

    ServiceResult recentMeals(const std::string& agentUid, int days) const;

   private:
    MySQLPool& m_pool;
};