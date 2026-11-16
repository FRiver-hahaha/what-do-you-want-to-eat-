#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "http/service/ServiceResult.h"

class MySQLPool;

// 健康档案读写：默认档案、字段校验/截断、BMI 现算与 source 标记都在此层完成。
class ProfileService {
   public:
    explicit ProfileService(MySQLPool& pool) : m_pool(pool) {}

    ServiceResult getProfile(const std::string& agentUid) const;
    ServiceResult patchProfile(const std::string& agentUid,
                               const nlohmann::json& body) const;

   private:
    MySQLPool& m_pool;
};