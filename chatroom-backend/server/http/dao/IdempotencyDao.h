#pragma once

#include <mysql/mysql.h>

#include <string>

// 幂等记录只保存判定所需的最小信息：payload 指纹 + 首次成功的 meal 主键。
struct IdempotencyRecord {
    std::string payloadHash;
    unsigned long long mealId = 0;
};

class IdempotencyDao {
   public:
    enum class Lookup { Found, NotFound, Unavailable };
    enum class Insert { Ok, Duplicate, Unavailable };

    static Lookup findOn(MYSQL* conn, const std::string& agentUid,
                         const std::string& idemKey, IdempotencyRecord& out);

    // 依赖 uk_uid_key(agent_uid, idem_key) 由数据库裁决并发：重复键返回 Duplicate。
    static Insert insertOn(MYSQL* conn, const std::string& agentUid,
                           const std::string& idemKey,
                           const std::string& payloadHash,
                           unsigned long long mealId);
};