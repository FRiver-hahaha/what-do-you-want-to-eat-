#include "server/business/food/FoodService.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "server/business/common/BusinessJson.h"
#include "server/business/food/FoodDao.h"
#include "server/business/profile/ProfileDao.h"
#include "server/business/user/UserDao.h"
#include "server/cache/CacheClient.h"
#include "server/cache/CacheKeyBuilder.h"

namespace eat::business {
namespace {

constexpr std::size_t kMaxQueryLength = 256;
constexpr std::size_t kMaxTags = 20;
constexpr std::size_t kMaxTagLength = 30;
constexpr int kDefaultLimit = 5;
constexpr int kMaxLimit = 16;

bool IsKnownMealType(const std::string& meal_type) {
    return meal_type.empty() || meal_type == "早餐" || meal_type == "午餐" ||
           meal_type == "晚餐" || meal_type == "加餐";
}

bool IsKnownNature(const std::string& nature) {
    return nature.empty() || nature == "recommend" || nature == "indulge";
}

std::string LowerAscii(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

std::string NormalizeSpaces(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (i + 2 < text.size() && static_cast<unsigned char>(text[i]) == 0xE3 &&
            static_cast<unsigned char>(text[i + 1]) == 0x80 &&
            static_cast<unsigned char>(text[i + 2]) == 0x80) {
            out.push_back(' ');
            i += 3;
            continue;
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

std::vector<std::string> SplitTokens(const std::string& text) {
    std::vector<std::string> tokens;
    const std::string normalized = NormalizeSpaces(text);
    std::string current;
    for (const char c : normalized) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

bool HasMealType(const protocol::JsonValue& meal_types, const std::string& meal_type) {
    for (const auto& item : meal_types.AsArray()) {
        if (item.IsString() && item.AsString() == meal_type) return true;
    }
    return false;
}

bool IntersectsKeywords(const std::string& keywords,
                        const std::vector<std::string>& tags) {
    const std::vector<std::string> words = SplitTokens(keywords);
    for (const auto& tag : tags) {
        for (const auto& word : words) {
            if (tag == word) return true;
        }
    }
    return false;
}

bool ContainsAny(const std::string& haystack, const std::vector<std::string>& needles) {
    for (const auto& needle : needles) {
        if (!needle.empty() && haystack.find(needle) != std::string::npos) return true;
    }
    return false;
}

protocol::JsonValue FoodJson(const FoodRow& food) {
    protocol::JsonValue item = protocol::JsonValue::Object();
    item.Set("id", protocol::JsonValue::String(food.id));
    item.Set("name", protocol::JsonValue::String(food.name));
    item.Set("category", protocol::JsonValue::String(food.category));
    item.Set("description", protocol::JsonValue::String(food.description));
    item.Set("calories", JsonNumber(food.calories));
    item.Set("protein", JsonNumber(food.protein));
    item.Set("fiber", JsonNumber(food.fiber));
    item.Set("veggies", JsonNumber(food.veggies));
    item.Set("water", JsonNumber(food.water));
    item.Set("nature", protocol::JsonValue::String(food.nature));
    item.Set("keywords", protocol::JsonValue::String(food.keywords));
    item.Set("meal_types", ParseStringArray(food.meal_types));
    item.Set("price", protocol::JsonValue::Int(food.price));
    item.Set("available", protocol::JsonValue::Bool(food.available));
    item.Set("image", protocol::JsonValue::String(food.image));
    return item;
}

bool ReadTagList(const protocol::JsonValue* value, std::vector<std::string>& out) {
    out.clear();
    if (value == nullptr || value->IsNull()) return true;
    if (!value->IsArray()) return false;
    for (const auto& item : value->AsArray()) {
        if (!item.IsString()) return false;
        out.push_back(item.AsString());
    }
    return true;
}

std::string TagHash(const std::vector<std::string>& tags) {
    std::string joined;
    for (const auto& tag : tags) joined += tag + "|";
    return joined;
}

struct Candidate {
    int hit = 0;
    FoodRow food;
};

}  // namespace

BusinessResult FoodService::Menu(const std::string& user_key,
                                 const protocol::JsonValue& params) {
    if (user_key.empty()) return errors::UserNotFound();
    std::string meal_type;
    const protocol::JsonValue* value = params.Find("meal_type");
    if (value != nullptr && !value->IsNull()) {
        if (!value->IsString()) return errors::InvalidParams("meal_type must be a string");
        meal_type = value->AsString();
    }
    if (!IsKnownMealType(meal_type)) {
        return errors::InvalidParams("meal_type must be one of 早餐/午餐/晚餐/加餐");
    }

    const UserDao::Lookup user_lookup = UserDao(pool_).FindByUserKey(user_key);
    if (user_lookup == UserDao::Lookup::Unavailable) return errors::Unavailable();
    if (user_lookup == UserDao::Lookup::NotFound) return errors::UserNotFound();

    if (cache_ != nullptr) {
        protocol::JsonValue cached;
        if (cache_->Read(cache_->keys().Menu(meal_type, "all", 1, 64), cached)) {
            return BusinessResult::Success(cached);
        }
    }

    FoodDao foods(pool_);
    std::vector<FoodRow> rows;
    if (foods.ListAvailable(rows) != FoodDao::Lookup::Found) return errors::Unavailable();

    protocol::JsonValue list = protocol::JsonValue::Array();
    for (const auto& row : rows) {
        const protocol::JsonValue types = ParseStringArray(row.meal_types);
        if (!meal_type.empty() && !HasMealType(types, meal_type)) continue;
        list.AsArray().push_back(FoodJson(row));
    }

    protocol::JsonValue data = protocol::JsonValue::Object();
    data.Set("user_id", protocol::JsonValue::String(user_key));
    data.Set("meal_type", protocol::JsonValue::String(meal_type.empty() ? "全部" : meal_type));
    data.Set("foods", list);
    data.Set("count", protocol::JsonValue::Int(static_cast<std::int64_t>(list.AsArray().size())));
    if (cache_ != nullptr) {
        cache_->Write(cache_->keys().Menu(meal_type, "all", 1, 64), data, "menu");
    }
    return BusinessResult::Success(data);
}

BusinessResult FoodService::Search(const std::string& user_key,
                                   const protocol::JsonValue& params) {
    if (user_key.empty()) return errors::UserNotFound();

    std::string query;
    const protocol::JsonValue* value = params.Find("query");
    if (value == nullptr) value = params.Find("q");
    if (value != nullptr && !value->IsNull()) {
        if (!value->IsString()) return errors::InvalidParams("query must be a string");
        query = value->AsString();
    }

    std::string meal_type;
    value = params.Find("meal_type");
    if (value != nullptr && !value->IsNull()) {
        if (!value->IsString()) return errors::InvalidParams("meal_type must be a string");
        meal_type = value->AsString();
    }

    std::string nature;
    value = params.Find("nature");
    if (value != nullptr && !value->IsNull()) {
        if (!value->IsString()) return errors::InvalidParams("nature must be a string");
        nature = value->AsString();
    }

    int limit = 0;
    value = params.Find("limit");
    if (value != nullptr && !value->IsNull()) {
        if (!value->IsInt()) return errors::InvalidParams("limit must be an integer");
        limit = static_cast<int>(value->AsInt());
    }

    std::vector<std::string> tags;
    if (!ReadTagList(params.Find("tags"), tags)) {
        return errors::InvalidParams("tags must be an array of strings");
    }

    if (!IsKnownMealType(meal_type)) {
        return errors::InvalidParams("meal_type must be one of 早餐/午餐/晚餐/加餐");
    }
    if (!IsKnownNature(nature)) {
        return errors::InvalidParams("nature must be recommend or indulge");
    }
    const int effective_limit = limit <= 0 ? kDefaultLimit : limit;
    if (effective_limit > kMaxLimit) {
        return errors::InvalidParams("limit must be in [1,16]");
    }
    if (query.size() > kMaxQueryLength) return errors::InvalidParams("query is too long");
    if (tags.size() > kMaxTags) return errors::InvalidParams("too many tags");
    for (const auto& tag : tags) {
        if (tag.size() > kMaxTagLength) return errors::InvalidParams("tag is too long");
    }

    const UserDao::Lookup user_lookup = UserDao(pool_).FindByUserKey(user_key);
    if (user_lookup == UserDao::Lookup::Unavailable) return errors::Unavailable();
    if (user_lookup == UserDao::Lookup::NotFound) return errors::UserNotFound();

    std::string cache_key;
    if (cache_ != nullptr) {
        const std::string signature =
            TagHash(tags) + "|q=" + LowerAscii(NormalizeSpaces(query));
        cache_key = cache_->keys().Search(
            meal_type, nature, cache::CacheKeyBuilder::HashText(signature),
            static_cast<std::uint32_t>(effective_limit), cache_->ProfileVersion(user_key));
        protocol::JsonValue cached;
        if (cache_->Read(cache_key, cached)) return BusinessResult::Success(cached);
    }

    ProfileDao profiles(pool_);
    HealthProfileRow profile;
    const ProfileDao::Lookup profile_lookup = profiles.Load(user_key, profile);
    if (profile_lookup == ProfileDao::Lookup::Unavailable) return errors::Unavailable();
    const std::vector<std::string> allergies = ParseStringArrayText(profile.allergies);
    const std::vector<std::string> dislikes = ParseStringArrayText(profile.dislikes);

    const std::vector<std::string> tokens = SplitTokens(LowerAscii(query));

    FoodDao foods(pool_);
    std::vector<FoodRow> rows;
    if (foods.ListAvailable(rows) != FoodDao::Lookup::Found) return errors::Unavailable();

    std::vector<Candidate> candidates;
    for (const auto& row : rows) {
        const protocol::JsonValue types = ParseStringArray(row.meal_types);
        if (!meal_type.empty() && !HasMealType(types, meal_type)) continue;
        if (!nature.empty() && row.nature != nature) continue;
        if (!tags.empty() && !IntersectsKeywords(row.keywords, tags)) continue;
        const std::string haystack = LowerAscii(row.name + " " + row.category + " " +
                                                row.description + " " + row.keywords);
        int hit = 0;
        for (const auto& token : tokens) {
            if (haystack.find(token) != std::string::npos) ++hit;
        }
        if (!tokens.empty() && hit == 0) continue;
        if (ContainsAny(haystack, allergies)) continue;
        if (ContainsAny(haystack, dislikes)) continue;
        candidates.push_back(Candidate{hit, row});
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.hit > b.hit; });

    const std::size_t take = static_cast<std::size_t>(
        std::min<int>(effective_limit, static_cast<int>(candidates.size())));
    protocol::JsonValue list = protocol::JsonValue::Array();
    for (std::size_t i = 0; i < take; ++i) {
        list.AsArray().push_back(FoodJson(candidates[i].food));
    }

    protocol::JsonValue data = protocol::JsonValue::Object();
    data.Set("user_id", protocol::JsonValue::String(user_key));
    data.Set("foods", list);
    data.Set("count", protocol::JsonValue::Int(static_cast<std::int64_t>(list.AsArray().size())));
    data.Set("tags_hash", protocol::JsonValue::String(TagHash(tags)));
    if (cache_ != nullptr && !cache_key.empty()) {
        cache_->Write(cache_key, data, "search");
    }
    return BusinessResult::Success(data);
}

}  // namespace eat::business
