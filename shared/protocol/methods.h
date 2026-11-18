#pragma once

#include <string>

namespace eat::protocol::method {

constexpr const char* kAuthRegister = "auth.register";
constexpr const char* kAuthLogin = "auth.login";
constexpr const char* kAuthRefresh = "auth.refresh";
constexpr const char* kAuthLogout = "auth.logout";
constexpr const char* kAuthRevokeAll = "auth.revoke_all";
constexpr const char* kAuthChangePassword = "auth.change_password";

constexpr const char* kProfileGet = "profile.get";
constexpr const char* kProfilePatch = "profile.patch";
constexpr const char* kProfileMediaGet = "profile.media.get";
constexpr const char* kProfileMediaSet = "profile.media.set";

constexpr const char* kMealCreate = "meal.create";
constexpr const char* kMealRecent = "meal.recent";
constexpr const char* kMealPatch = "meal.patch";
constexpr const char* kMealDelete = "meal.delete";

constexpr const char* kStatsNutritionGap = "stats.nutrition_gap";
constexpr const char* kStatsWeekly = "stats.weekly";

constexpr const char* kFoodMenu = "food.menu";
constexpr const char* kFoodSearch = "food.search";

constexpr const char* kFriendList = "friend.list";
constexpr const char* kFriendRequest = "friend.request";
constexpr const char* kFriendAccept = "friend.accept";
constexpr const char* kFriendRemove = "friend.remove";
constexpr const char* kFriendBlock = "friend.block";

constexpr const char* kRankingFriends = "ranking.friends";

constexpr const char* kSyncPull = "sync.pull";

inline bool IsWriteMethod(const std::string& name) {
    return name == kAuthRegister || name == kMealCreate || name == kMealPatch ||
           name == kMealDelete || name == kProfilePatch || name == kProfileMediaSet ||
           name == kFriendRequest || name == kFriendAccept || name == kFriendRemove ||
           name == kFriendBlock;
}

}  // namespace eat::protocol::method
