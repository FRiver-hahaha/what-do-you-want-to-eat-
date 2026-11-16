#pragma once

#include <string>

class MySQLPool;

// health_profile 以 agent_uid 为主键，不依赖 MySQL int id。
struct HealthProfileRow {
    std::string name;
    std::string gender;
    int age = 0;
    double height = 0;
    double weight = 0;
    std::string goal = "均衡饮食";
    std::string activity = "久坐";
    std::string dietStyle = "普通饮食";
    int waterTarget = 1800;
    double sleepHours = 7;
    std::string allergies = "[]";
    std::string dislikes = "[]";
    int spendMin = 20;
    int spendMax = 35;
    std::string note;
};

class ProfileDao {
   public:
    enum class Lookup { Found, NotFound, Unavailable };

    explicit ProfileDao(MySQLPool& pool) : m_pool(pool) {}

    Lookup load(const std::string& agentUid, HealthProfileRow& out) const;
    bool save(const std::string& agentUid, const HealthProfileRow& row) const;

   private:
    MySQLPool& m_pool;
};