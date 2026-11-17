#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "server/cache/CacheKeyBuilder.h"

namespace eat::cache {

std::vector<std::string> ProfileKeys(const CacheKeyBuilder& builder,
                                     const std::string& user_key);

std::vector<std::string> MealKeys(const CacheKeyBuilder& builder,
                                  const std::string& user_key,
                                  const std::vector<std::string>& dates,
                                  const std::vector<std::string>& iso_weeks,
                                  std::uint64_t prof_version);

std::vector<std::string> FriendKeys(const CacheKeyBuilder& builder,
                                    const std::string& owner_key,
                                    const std::string& peer_key);

}  // namespace eat::cache