#pragma once

#include <algorithm>
#include <cmath>
#include <string>

namespace eat::business {

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

inline double NutritionRatio(double value, double target) {
    if (target <= 0) return 1.0;
    return std::min(value / target, 1.0);
}

inline int ActivityBonus(const std::string& activity) {
    if (activity == "较高") return 3;
    if (activity == "适中") return 2;
    return 0;
}

inline long DailyScore(const DayNutrition& nutrition, const std::string& activity) {
    const double raw = 68.0 + NutritionRatio(nutrition.veggies, kVeggiesTarget) * 8.0 +
                       NutritionRatio(nutrition.protein, kProteinTarget) * 6.0 +
                       NutritionRatio(nutrition.fiber, kFiberTarget) * 5.0 +
                       std::min(nutrition.meals, 3) + ActivityBonus(activity) -
                       nutrition.penalty;
    long score = std::lround(raw);
    if (score < 45) score = 45;
    if (score > 96) score = 96;
    return score;
}

inline bool IsMainMeal(const std::string& type) {
    return type == "早餐" || type == "午餐" || type == "晚餐";
}

}  // namespace eat::business
