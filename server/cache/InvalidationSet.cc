#include "server/cache/InvalidationSet.h"

namespace eat::cache {

std::vector<std::string> ProfileKeys(const CacheKeyBuilder& builder,
                                     const std::string& user_key) {
    return {builder.Profile(user_key)};
}

std::vector<std::string> MealKeys(const CacheKeyBuilder& builder,
                                  const std::string& user_key,
                                  const std::vector<std::string>& dates,
                                  const std::vector<std::string>& iso_weeks,
                                  std::uint64_t prof_version) {
    std::vector<std::string> keys;
    keys.reserve(dates.size() * 2 + iso_weeks.size());
    for (const auto& date : dates) {
        keys.push_back(builder.Meal(user_key, date, date, "latest"));
        keys.push_back(builder.Gap(user_key, date, prof_version));
    }
    for (const auto& week : iso_weeks) {
        keys.push_back(builder.Week(user_key, week, prof_version));
    }
    return keys;
}

std::vector<std::string> FriendKeys(const CacheKeyBuilder& builder,
                                    const std::string& owner_key,
                                    const std::string& peer_key) {
    return {builder.Friend(owner_key), builder.Friend(peer_key)};
}

}  // namespace eat::cache