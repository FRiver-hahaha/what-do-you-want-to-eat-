#pragma once

#include <algorithm>
#include <cmath>
#include <string>

// 健康分口径：与 StatsService::dailyScore 完全一致（S2 已按前端
// getHealthAnalysis 逐字复刻并验证）。排行榜复用同一公式，避免两处漂移。
namespace healthscore {

constexpr double kProteinTarget = 90.0;
constexpr double kFiberTarget = 25.0;
constexpr double kVeggiesTarget = 500.0;

struct DayNutrition {
    double protein = 0;
    double fiber = 0;
    double veggies = 0;
    double water = 0;
    double penalty = 0;
    int meals = 0;
};

inline double ratio(double value, double target) {
    if (target <= 0) {
        return 1.0;
    }
    return std::min(value / target, 1.0);
}

inline int activityBonus(const std::string& activity) {
    if (activity == "较高") {
        return 3;
    }
    if (activity == "适中") {
        return 2;
    }
    return 0;
}

// clamp(45,96, round(68 + veg*8 + protein*6 + fiber*5 + min(餐数,3)
//                    + 活动加成 - 罚分))
inline long dailyScore(const DayNutrition& nutrition,
                       const std::string& activity) {
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

inline bool isMainMeal(const std::string& type) {
    return type == "早餐" || type == "午餐" || type == "晚餐";
}

}  // namespace healthscore