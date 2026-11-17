#pragma once

#include <string>

#include "server/database/MySQLPool.h"
#include "shared/protocol/JsonValue.h"

namespace eat::business {

struct HealthProfileRow {
    std::string name = "我";
    std::string gender = "女";
    int age = 28;
    double height = 170;
    double weight = 60;
    std::string goal = "均衡饮食";
    std::string activity = "久坐";
    std::string diet_style = "普通饮食";
    int water_target = 1800;
    double sleep_hours = 7;
    std::string allergies = "[\"花生\"]";
    std::string dislikes = "[\"香菜\"]";
    int spend_min = 20;
    int spend_max = 35;
    std::string note = "下午容易口渴，晚餐不宜过饱";
};

// health_profile keeps its P8 column name (agent_uid) until the 007 migration
// renames it; the value stored there is the unified user_key.
class ProfileDao {
 public:
    enum class Lookup { Found, NotFound, Unavailable };

    explicit ProfileDao(db::MySQLPool& pool) : pool_(pool) {}

    Lookup Load(const std::string& user_key, HealthProfileRow& out);
    bool Save(const std::string& user_key, const HealthProfileRow& row);

 private:
    db::MySQLPool& pool_;
};

}  // namespace eat::business