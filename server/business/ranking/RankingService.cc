#include "server/business/ranking/RankingService.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "server/business/common/BusinessJson.h"
#include "server/business/common/DateUtil.h"
#include "server/business/common/HealthScore.h"
#include "server/business/meal/MealDao.h"
#include "server/business/profile/ProfileDao.h"
#include "server/business/ranking/RankingDao.h"
#include "server/business/user/UserDao.h"

namespace eat::business {
namespace {

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

struct Entry {
    std::string user_key;
    std::string name;
    double score = 0;
};

}  // namespace

bool RankingService::ScoreFor(const std::string& user_key, const std::string& scope,
                              double& out) const {
    ProfileDao profiles(pool_);
    HealthProfileRow profile;
    const ProfileDao::Lookup profile_lookup = profiles.Load(user_key, profile);
    if (profile_lookup == ProfileDao::Lookup::Unavailable) return false;
    const std::string activity =
        profile_lookup == ProfileDao::Lookup::Found ? profile.activity : "久坐";

    const std::string today = dateutil::Today();
    const std::string from_date = scope == "week" ? dateutil::WeekStart(today) : today;

    MealDao meals(pool_);
    std::vector<MealRow> rows;
    if (meals.ListRange(user_key, from_date, today, rows) != MealDao::Lookup::Found) {
        return false;
    }

    if (scope == "today") {
        out = static_cast<double>(DailyScore(SumNutrition(rows), activity));
        return true;
    }

    std::map<std::string, std::vector<MealRow>> by_date;
    for (const auto& row : rows) by_date[row.date].push_back(row);
    if (by_date.empty()) {
        out = 0.0;
        return true;
    }
    double sum = 0;
    for (const auto& entry : by_date) {
        sum += static_cast<double>(DailyScore(SumNutrition(entry.second), activity));
    }
    out = std::round(sum / static_cast<double>(by_date.size()) * 10.0) / 10.0;
    return true;
}

BusinessResult RankingService::Friends(const std::string& user_key,
                                       const protocol::JsonValue& params) {
    if (user_key.empty()) return errors::UserNotFound();

    std::string scope = "week";
    const protocol::JsonValue* value = params.Find("scope");
    if (value != nullptr && !value->IsNull()) {
        if (!value->IsString()) return errors::InvalidParams("scope must be a string");
        scope = value->AsString();
    }
    if (scope != "week" && scope != "today") {
        return errors::InvalidParams("scope must be week or today");
    }

    const UserDao::Lookup user_lookup = UserDao(pool_).FindByUserKey(user_key);
    if (user_lookup == UserDao::Lookup::Unavailable) return errors::Unavailable();
    if (user_lookup == UserDao::Lookup::NotFound) return errors::UserNotFound();

    std::vector<RankingMember> friends;
    if (RankingDao(pool_).ListFriends(user_key, friends) != RankingDao::Lookup::Found) {
        return errors::Unavailable();
    }

    std::vector<Entry> entries;
    Entry self;
    self.user_key = user_key;
    self.name = "我";
    if (!ScoreFor(user_key, scope, self.score)) return errors::Unavailable();
    entries.push_back(self);

    for (const auto& friend_row : friends) {
        Entry entry;
        entry.user_key = friend_row.user_key;
        entry.name = friend_row.name.empty() ? friend_row.user_key : friend_row.name;
        if (!ScoreFor(friend_row.user_key, scope, entry.score)) return errors::Unavailable();
        entries.push_back(entry);
    }

    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.user_key < b.user_key;
    });

    protocol::JsonValue list = protocol::JsonValue::Array();
    int rank = 0;
    double previous = 0;
    bool has_previous = false;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (!has_previous || std::fabs(entries[i].score - previous) > 1e-9) {
            rank = static_cast<int>(i) + 1;
            previous = entries[i].score;
            has_previous = true;
        }
        protocol::JsonValue item = protocol::JsonValue::Object();
        item.Set("rank", protocol::JsonValue::Int(rank));
        item.Set("user_id", protocol::JsonValue::String(entries[i].user_key));
        item.Set("name", protocol::JsonValue::String(entries[i].name));
        item.Set("score", JsonNumber(entries[i].score));
        list.AsArray().push_back(item);
    }

    protocol::JsonValue data = protocol::JsonValue::Object();
    data.Set("user_id", protocol::JsonValue::String(user_key));
    data.Set("scope", protocol::JsonValue::String(scope));
    data.Set("ranking", list);
    return BusinessResult::Success(data);
}

}  // namespace eat::business
