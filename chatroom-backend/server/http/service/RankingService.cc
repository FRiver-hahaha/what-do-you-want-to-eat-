#include "http/service/RankingService.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/MySQLPool.h"
#include "http/DateUtil.h"
#include "http/JsonUtil.h"
#include "http/dao/MealDao.h"
#include "http/dao/ProfileDao.h"
#include "http/dao/RankingDao.h"
#include "http/dao/UserDao.h"
#include "http/service/HealthScore.h"

namespace {

constexpr int kDefaultWaterTarget = 1800;

ServiceResult makeError(ServiceResult::Status status,
                        const std::string& message) {
    ServiceResult result;
    result.status = status;
    result.message = message;
    return result;
}

healthscore::DayNutrition sumNutrition(const std::vector<MealRow>& rows) {
    healthscore::DayNutrition total;
    total.meals = static_cast<int>(rows.size());
    for (const auto& row : rows) {
        total.protein += row.protein;
        total.fiber += row.fiber;
        total.veggies += row.veggies;
        total.water += row.water;
        total.penalty += row.penalty;
    }
    return total;
}

struct Entry {
    std::string agentUid;
    std::string name;
    double score = 0;
};

}  // namespace

bool RankingService::scoreFor(const std::string& agentUid,
                              const std::string& scope, double& out) const {
    ProfileDao profiles(m_pool);
    HealthProfileRow profile;
    const ProfileDao::Lookup profileLookup = profiles.load(agentUid, profile);
    if (profileLookup == ProfileDao::Lookup::Unavailable) {
        return false;
    }
    // 没有档案的好友按默认久坐计算，不因缺行把整张榜打空。
    const std::string activity =
        profileLookup == ProfileDao::Lookup::Found ? profile.activity : "久坐";

    const std::string today = httpdate::today();
    const std::string fromDate =
        scope == "week" ? httpdate::weekStart(today) : today;

    MealDao meals(m_pool);
    std::vector<MealRow> rows;
    if (meals.listRange(agentUid, fromDate, today, rows) !=
        MealDao::Lookup::Found) {
        return false;
    }

    if (scope == "today") {
        out = static_cast<double>(
            healthscore::dailyScore(sumNutrition(rows), activity));
        return true;
    }

    // 周榜：只对「有记录的天」取平均；整周无记录记 0 分（与周统计口径一致）。
    std::map<std::string, std::vector<MealRow>> byDate;
    for (const auto& row : rows) {
        byDate[row.date].push_back(row);
    }
    if (byDate.empty()) {
        out = 0.0;
        return true;
    }
    double sum = 0;
    for (const auto& entry : byDate) {
        sum += static_cast<double>(
            healthscore::dailyScore(sumNutrition(entry.second), activity));
    }
    out = std::round(sum / static_cast<double>(byDate.size()) * 10.0) / 10.0;
    return true;
}

ServiceResult RankingService::friendRanking(const std::string& agentUid,
                                            const std::string& scope) const {
    if (scope != "week" && scope != "today") {
        return makeError(ServiceResult::Status::InvalidParams,
                         "scope must be week or today");
    }

    UserDao users(m_pool);
    const UserDao::Lookup userLookup = users.findByAgentUid(agentUid);
    if (userLookup == UserDao::Lookup::Unavailable) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }
    if (userLookup == UserDao::Lookup::NotFound) {
        return makeError(ServiceResult::Status::UserNotFound,
                         "user not found");
    }

    RankingDao ranking(m_pool);
    std::vector<RankingMember> friends;
    if (ranking.listFriends(agentUid, friends) != RankingDao::Lookup::Found) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }

    std::vector<Entry> entries;
    Entry self;
    self.agentUid = agentUid;
    // 与 mock 保持一致：自己在榜单中的展示名固定为「我」。
    self.name = "我";
    if (!scoreFor(agentUid, scope, self.score)) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }
    entries.push_back(self);

    for (const auto& friendRow : friends) {
        Entry entry;
        entry.agentUid = friendRow.agentUid;
        entry.name = friendRow.name.empty() ? friendRow.agentUid : friendRow.name;
        if (!scoreFor(friendRow.agentUid, scope, entry.score)) {
            return makeError(ServiceResult::Status::Unavailable,
                             "database unavailable");
        }
        entries.push_back(entry);
    }

    // 分数降序；同分按 agent_uid 升序，保证结果稳定可复现。
    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry& a, const Entry& b) {
                         if (a.score != b.score) {
                             return a.score > b.score;
                         }
                         return a.agentUid < b.agentUid;
                     });

    nlohmann::json list = nlohmann::json::array();
    int rank = 0;
    double previous = 0;
    bool hasPrevious = false;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        // 并列名次：分数相同共享名次，下一名跳到「已上榜人数 + 1」。
        if (!hasPrevious || std::fabs(entries[i].score - previous) > 1e-9) {
            rank = static_cast<int>(i) + 1;
            previous = entries[i].score;
            hasPrevious = true;
        }
        nlohmann::json item;
        item["rank"] = rank;
        item["user_id"] = entries[i].agentUid;
        item["name"] = entries[i].name;
        item["score"] = jsonutil::number(entries[i].score);
        list.push_back(item);
    }

    ServiceResult result;
    result.status = ServiceResult::Status::Ok;
    result.data["user_id"] = agentUid;
    result.data["scope"] = scope;
    result.data["ranking"] = list;
    return result;
}