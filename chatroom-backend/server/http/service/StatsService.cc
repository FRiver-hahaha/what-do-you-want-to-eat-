#include "http/service/StatsService.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "database/MySQLPool.h"
#include "http/DateUtil.h"
#include "http/JsonUtil.h"
#include "http/dao/MealDao.h"
#include "http/dao/ProfileDao.h"
#include "http/dao/UserDao.h"

namespace {

constexpr double kProteinTarget = 90.0;
constexpr double kFiberTarget = 25.0;
constexpr double kVeggiesTarget = 500.0;
constexpr int kDefaultWaterTarget = 1800;
constexpr int kStreakWindowDays = 90;

ServiceResult makeError(ServiceResult::Status status,
                        const std::string& message) {
    ServiceResult result;
    result.status = status;
    result.message = message;
    return result;
}

struct Nutrition {
    double protein = 0;
    double fiber = 0;
    double veggies = 0;
    double water = 0;
    double penalty = 0;
    int meals = 0;
};

Nutrition sumNutrition(const std::vector<MealRow>& rows) {
    Nutrition total;
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

double ratio(double value, double target) {
    if (target <= 0) {
        return 1.0;
    }
    return std::min(value / target, 1.0);
}

int activityBonus(const std::string& activity) {
    if (activity == "较高") {
        return 3;
    }
    if (activity == "适中") {
        return 2;
    }
    return 0;
}

// 逐字复刻前端 getHealthAnalysis：
// clamp(45,96, round(68 + veg*8 + protein*6 + fiber*5 + min(餐数,3)
//                    + 活动加成 - 罚分))
long dailyScore(const Nutrition& nutrition, const std::string& activity) {
    const double raw =
        68.0 + ratio(nutrition.veggies, kVeggiesTarget) * 8.0 +
        ratio(nutrition.protein, kProteinTarget) * 6.0 +
        ratio(nutrition.fiber, kFiberTarget) * 5.0 +
        std::min(nutrition.meals, 3) + activityBonus(activity) -
        nutrition.penalty;
    long score = std::lround(raw);
    if (score < 45) {
        score = 45;
    }
    if (score > 96) {
        score = 96;
    }
    return score;
}

bool isMainMeal(const std::string& type) {
    return type == "早餐" || type == "午餐" || type == "晚餐";
}

}  // namespace

ServiceResult StatsService::nutritionGap(const std::string& agentUid,
                                         const std::string& date) const {
    const std::string targetDate = date.empty() ? httpdate::today() : date;
    if (!httpdate::validDate(targetDate)) {
        return makeError(ServiceResult::Status::InvalidParams,
                         "date must be a valid YYYY-MM-DD within 2000-2100");
    }

    UserDao users(m_pool);
    const UserDao::Lookup userLookup = users.findByAgentUid(agentUid);
    if (userLookup == UserDao::Lookup::Unavailable) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }
    if (userLookup == UserDao::Lookup::NotFound) {
        return makeError(ServiceResult::Status::UserNotFound, "user not found");
    }

    ProfileDao profiles(m_pool);
    HealthProfileRow profile;  // 默认行：water_target=1800
    const ProfileDao::Lookup profileLookup = profiles.load(agentUid, profile);
    if (profileLookup == ProfileDao::Lookup::Unavailable) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }
    const int waterTarget =
        profile.waterTarget > 0 ? profile.waterTarget : kDefaultWaterTarget;

    MealDao meals(m_pool);
    std::vector<MealRow> rows;
    const MealDao::Lookup lookup =
        meals.listRange(agentUid, targetDate, targetDate, rows);
    if (lookup != MealDao::Lookup::Found) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }

    const Nutrition intake = sumNutrition(rows);
    const double targets[] = {kProteinTarget, kFiberTarget, kVeggiesTarget,
                              static_cast<double>(waterTarget)};
    const double values[] = {intake.protein, intake.fiber, intake.veggies,
                             intake.water};
    static const char* const kKeys[] = {"protein", "fiber", "veggies", "water"};

    nlohmann::json targetJson = nlohmann::json::object();
    nlohmann::json intakeJson = nlohmann::json::object();
    nlohmann::json gapJson = nlohmann::json::object();
    for (int i = 0; i < 4; ++i) {
        targetJson[kKeys[i]] = jsonutil::number(targets[i]);
        intakeJson[kKeys[i]] = jsonutil::number(values[i]);
        gapJson[kKeys[i]] =
            jsonutil::number(std::max(0.0, targets[i] - values[i]));
    }

    ServiceResult result;
    result.status = ServiceResult::Status::Ok;
    result.data["user_id"] = agentUid;
    result.data["date"] = targetDate;
    result.data["targets"] = targetJson;
    result.data["intake"] = intakeJson;
    result.data["gap"] = gapJson;
    return result;
}

ServiceResult StatsService::weeklyStats(const std::string& agentUid) const {
    UserDao users(m_pool);
    const UserDao::Lookup userLookup = users.findByAgentUid(agentUid);
    if (userLookup == UserDao::Lookup::Unavailable) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }
    if (userLookup == UserDao::Lookup::NotFound) {
        return makeError(ServiceResult::Status::UserNotFound, "user not found");
    }

    ProfileDao profiles(m_pool);
    HealthProfileRow profile;  // 默认 activity=久坐（加成 0）
    const ProfileDao::Lookup profileLookup = profiles.load(agentUid, profile);
    if (profileLookup == ProfileDao::Lookup::Unavailable) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }
    const std::string activity = profile.activity;

    const std::string today = httpdate::today();
    const std::string weekStart = httpdate::weekStart(today);
    const std::string streakStart =
        httpdate::addDays(today, -(kStreakWindowDays - 1));
    // 一次查询同时服务周统计与连续天数：取两者更早的起点。
    const std::string fromDate =
        weekStart < streakStart ? weekStart : streakStart;

    MealDao meals(m_pool);
    std::vector<MealRow> rows;
    if (meals.listRange(agentUid, fromDate, today, rows) !=
        MealDao::Lookup::Found) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }

    std::map<std::string, std::vector<MealRow>> byDate;
    for (const auto& row : rows) {
        byDate[row.date].push_back(row);
    }

    int daysRecorded = 0;
    int threeMealDays = 0;
    int points = 0;
    double scoreSum = 0;
    nlohmann::json daily = nlohmann::json::array();

    for (const auto& entry : byDate) {
        if (entry.first < weekStart || entry.first > today) {
            continue;
        }
        const Nutrition nutrition = sumNutrition(entry.second);
        const long score = dailyScore(nutrition, activity);
        ++daysRecorded;
        scoreSum += static_cast<double>(score);

        std::set<std::string> types;
        for (const auto& meal : entry.second) {
            if (isMainMeal(meal.type)) {
                types.insert(meal.type);
            }
        }
        if (types.size() == 3) {
            ++threeMealDays;
        }
        // 复刻前端 getMealPoints + 每日打卡：三餐类型去重数 ×10，
        // 满三餐额外 +20，当天有记录再 +10 打卡分。
        points += 10 + static_cast<int>(types.size()) * 10 +
                  (types.size() == 3 ? 20 : 0);

        nlohmann::json day;
        day["date"] = entry.first;
        day["score"] = score;
        day["meals"] = nutrition.meals;
        day["penalty"] = jsonutil::number(nutrition.penalty);
        daily.push_back(day);
    }

    // 连续记录天数：今天有记录从今天起算，否则从昨天起算；遇到空档即停止。
    int streak = 0;
    const std::string anchor =
        byDate.count(today) != 0 ? today : httpdate::addDays(today, -1);
    for (int i = 0; i < kStreakWindowDays; ++i) {
        if (byDate.count(httpdate::addDays(anchor, -i)) == 0) {
            break;
        }
        ++streak;
    }

    const double average =
        daysRecorded > 0
            ? std::round(scoreSum / static_cast<double>(daysRecorded) * 10.0) /
                  10.0
            : 0.0;

    ServiceResult result;
    result.status = ServiceResult::Status::Ok;
    result.data["user_id"] = agentUid;
    result.data["week"] = weekStart;
    result.data["avg_health_score"] = jsonutil::number(average);
    result.data["days_recorded"] = daysRecorded;
    result.data["three_meal_days"] = threeMealDays;
    result.data["points"] = points;
    result.data["streak"] = streak;
    result.data["daily"] = daily;
    return result;
}