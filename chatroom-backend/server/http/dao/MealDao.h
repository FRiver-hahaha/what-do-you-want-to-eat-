#pragma once

#include <mysql/mysql.h>

#include <string>
#include <vector>

class MySQLPool;

// meal_records 的一行；items 保持 JSON 文本形态，由 Service 层决定对外的数组结构。
struct MealRow {
    unsigned long long id = 0;
    std::string date;
    std::string type = "加餐";
    std::string time = "00:00";
    std::string text;
    std::string items = "[]";
    double protein = 0;
    double fiber = 0;
    double veggies = 0;
    double water = 0;
    int penalty = 0;
};

class MealDao {
   public:
    enum class Lookup { Found, NotFound, Unavailable };

    explicit MealDao(MySQLPool& pool) : m_pool(pool) {}

    // 事务内写入：调用方持有连接并负责 commit/rollback。失败返回 0。
    static unsigned long long insertOn(MYSQL* conn, const std::string& agentUid,
                                       const MealRow& row);

    // 事务内按主键回读（幂等命中时重放首次结果）。
    static Lookup loadByIdOn(MYSQL* conn, const std::string& agentUid,
                             unsigned long long id, MealRow& out);

    // 事务内按闭区间日期查询，按 date、id 升序（与 Agent 期望的时间顺序一致）。
    static Lookup listRangeOn(MYSQL* conn, const std::string& agentUid,
                              const std::string& fromDate,
                              const std::string& toDate,
                              std::vector<MealRow>& out);

    Lookup listRange(const std::string& agentUid, const std::string& fromDate,
                     const std::string& toDate,
                     std::vector<MealRow>& out) const;

   private:
    MySQLPool& m_pool;
};