#include "server/business/stats/StatsService.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "server/business/common/BusinessJson.h"
#include "server/business/common/DateUtil.h"
#include "server/business/common/HealthScore.h"
#include "server/business/meal/MealDao.h"
#include "server/business/profile/ProfileDao.h"
#include "server/business/user/UserDao.h"
#include "server/cache/CacheClient.h"

namespace eat::business {
namespace {

constexpr int kDefaultWaterTarget = 1800;
constexpr int kStreakWindowDays = 90;

DayNutrition SumNutrition(const std::vector<MealRow>& rows) {
    DayNutrition total;
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

bool EnsureUser(db::MySQLPool& pool, const std::string& user_key) {
    return UserDao(pool).FindByUserKey(user_key) == UserDao::Lookup::Found;
}

}  // namespace

BusinessResult StatsService::NutritionGap(const std::string& user_key,
                                          const protocol::JsonValue& params) {
    if (user_key.empty()) return errors::UserNotFound();

    std::string target_date = dateutil::Today();
    const protocol::JsonValue* date_value = params.Find("date");
    if (date_value != nullptr && !date_value->IsNull()) {
        if (!date_value->IsString()) {
            return errors::InvalidParams("date must be a YYYY-MM-DD string");
        }
        target_date = date_value->AsString();
    }
    if (!dateutil::ValidDate(target_date)) {
        return errors::InvalidParams("date must be a valid YYYY-MM-DD within 2000-2100");
    }

    UserDao users(pool_);
    const UserDao::Lookup user_lookup = users.FindByUserKey(user_key);
    if (user_lookup == UserDao::Lookup::Unavailable) return errors::Unavailable();
    if (user_lookup == UserDao::Lookup::NotFound) return errors::UserNotFound();

    if (cache_ != nullptr) {
        protocol::JsonValue cached;
        const std::string cache_key =
            cache_->keys().Gap(user_key, target_date, cache_->ProfileVersion(user_key));
        if (cache_->Read(cache_key, cached)) return BusinessResult::Success(cached);
    }

    ProfileDao profiles(pool_);
    HealthProfileRow profile;
    const ProfileDao::Lookup profile_lookup = profiles.Load(user_key, profile);
    if (profile_lookup == ProfileDao::Lookup::Unavailable) return errors::Unavailable();
    const int water_target =
        profile.water_target > 0 ? profile.water_target : kDefaultWaterTarget;

    MealDao meals(pool_);
    std::vector<MealRow> rows;
    if (meals.ListRange(user_key, target_date, target_date, rows) !=
        MealDao::Lookup::Found) {
        return errors::Unavailable();
    }

    const DayNutrition intake = SumNutrition(rows);
    const double targets[] = {kProteinTarget, kFiberTarget, kVeggiesTarget,
                              static_cast<double>(water_target)};
    const double values[] = {intake.protein, intake.fiber, intake.veggies, intake.water};
    static const char* const kKeys[] = {"protein", "fiber", "veggies", "water"};

    protocol::JsonValue target_json = protocol::JsonValue::Object();
    protocol::JsonValue intake_json = protocol::JsonValue::Object();
    protocol::JsonValue gap_json = protocol::JsonValue::Object();
    for (int i = 0; i < 4; ++i) {
        target_json.Set(kKeys[i], JsonNumber(targets[i]));
        intake_json.Set(kKeys[i], JsonNumber(values[i]));
        gap_json.Set(kKeys[i], JsonNumber(std::max(0.0, targets[i] - values[i])));
    }

    protocol::JsonValue data = protocol::JsonValue::Object();
    data.Set("user_id", protocol::JsonValue::String(user_key));
    data.Set("date", protocol::JsonValue::String(target_date));
    data.Set("targets", target_json);
    data.Set("intake", intake_json);
    data.Set("gap", gap_json);
    if (cache_ != nullptr) {
        cache_->Write(cache_->keys().Gap(user_key, target_date,
                                         cache_->ProfileVersion(user_key)),
                      data, "gap");
    }
    return BusinessResult::Success(data);
}

BusinessResult StatsService::Weekly(const std::string& user_key,
                                    const protocol::JsonValue&) {
    if (user_key.empty()) return errors::UserNotFound();

    UserDao users(pool_);
    const UserDao::Lookup user_lookup = users.FindByUserKey(user_key);
    if (user_lookup == UserDao::Lookup::Unavailable) return errors::Unavailable();
    if (user_lookup == UserDao::Lookup::NotFound) return errors::UserNotFound();

    ProfileDao profiles(pool_);
    HealthProfileRow profile;
    const ProfileDao::Lookup profile_lookup = profiles.Load(user_key, profile);
    if (profile_lookup == ProfileDao::Lookup::Unavailable) return errors::Unavailable();
    const std::string activity = profile.activity;

    const std::string today = dateutil::Today();
    const std::string week_start = dateutil::WeekStart(today);
    if (cache_ != nullptr) {
        protocol::JsonValue cached;
        const std::string cache_key =
            cache_->keys().Week(user_key, week_start, cache_->ProfileVersion(user_key));
        if (cache_->Read(cache_key, cached)) return BusinessResult::Success(cached);
    }
    const std::string streak_start = dateutil::AddDays(today, -(kStreakWindowDays - 1));
    const std::string from_date = week_start < streak_start ? week_start : streak_start;

    MealDao meals(pool_);
    std::vector<MealRow> rows;
    if (meals.ListRange(user_key, from_date, today, rows) != MealDao::Lookup::Found) {
        return errors::Unavailable();
    }

    std::map<std::string, std::vector<MealRow>> by_date;
    for (const auto& row : rows) by_date[row.date].push_back(row);

    int days_recorded = 0;
    int three_meal_days = 0;
    int points = 0;
    double score_sum = 0;
    protocol::JsonValue daily = protocol::JsonValue::Array();

    for (const auto& entry : by_date) {
        if (entry.first < week_start || entry.first > today) continue;
        const DayNutrition nutrition = SumNutrition(entry.second);
        const long score = DailyScore(nutrition, activity);
        ++days_recorded;
        score_sum += static_cast<double>(score);

        std::set<std::string> types;
        for (const auto& meal : entry.second) {
            if (IsMainMeal(meal.type)) types.insert(meal.type);
        }
        if (types.size() == 3) ++three_meal_days;
        points += 10 + static_cast<int>(types.size()) * 10 +
                  (types.size() == 3 ? 20 : 0);

        protocol::JsonValue day = protocol::JsonValue::Object();
        day.Set("date", protocol::JsonValue::String(entry.first));
        day.Set("score", protocol::JsonValue::Int(score));
        day.Set("meals", protocol::JsonValue::Int(nutrition.meals));
        day.Set("penalty", JsonNumber(nutrition.penalty));
        daily.AsArray().push_back(day);
    }

    int streak = 0;
    const std::string anchor =
        by_date.count(today) != 0 ? today : dateutil::AddDays(today, -1);
    for (int i = 0; i < kStreakWindowDays; ++i) {
        if (by_date.count(dateutil::AddDays(anchor, -i)) == 0) break;
        ++streak;
    }

    const double average =
        days_recorded > 0
            ? std::round(score_sum / static_cast<double>(days_recorded) * 10.0) / 10.0
            : 0.0;

    protocol::JsonValue data = protocol::JsonValue::Object();
    data.Set("user_id", protocol::JsonValue::String(user_key));
    data.Set("week", protocol::JsonValue::String(week_start));
    data.Set("avg_health_score", JsonNumber(average));
    data.Set("days_recorded", protocol::JsonValue::Int(days_recorded));
    data.Set("three_meal_days", protocol::JsonValue::Int(three_meal_days));
    data.Set("points", protocol::JsonValue::Int(points));
    data.Set("streak", protocol::JsonValue::Int(streak));
    data.Set("daily", daily);
    if (cache_ != nullptr) {
        cache_->Write(cache_->keys().Week(user_key, week_start,
                                          cache_->ProfileVersion(user_key)),
                      data, "week");
    }
    return BusinessResult::Success(data);
}

}  // namespace eat::business
