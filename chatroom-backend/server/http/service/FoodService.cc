#include "http/service/FoodService.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/MySQLPool.h"
#include "http/Auth.h"
#include "http/JsonUtil.h"
#include "http/dao/FoodDao.h"
#include "http/dao/ProfileDao.h"
#include "http/dao/UserDao.h"

namespace {

constexpr std::size_t kMaxQueryLength = 256;
constexpr std::size_t kMaxTags = 20;
constexpr std::size_t kMaxTagLength = 30;
constexpr int kDefaultLimit = 5;
constexpr int kMaxLimit = 16;

ServiceResult makeError(ServiceResult::Status status,
                        const std::string& message) {
    ServiceResult result;
    result.status = status;
    result.message = message;
    return result;
}

bool isKnownMealType(const std::string& mealType) {
    return mealType.empty() || mealType == "早餐" || mealType == "午餐" ||
           mealType == "晚餐" || mealType == "加餐";
}

bool isKnownNature(const std::string& nature) {
    return nature.empty() || nature == "recommend" || nature == "indulge";
}

std::string lowerAscii(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return value;
}

// Python 的 str.split() 把全角空格也当分隔符，这里做同样处理。
std::string normalizeSpaces(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (i + 2 < text.size() &&
            static_cast<unsigned char>(text[i]) == 0xE3 &&
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

std::vector<std::string> splitTokens(const std::string& text) {
    std::vector<std::string> tokens;
    const std::string normalized = normalizeSpaces(text);
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
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

std::vector<std::string> parseStringArray(const std::string& raw) {
    std::vector<std::string> values;
    try {
        const nlohmann::json parsed = nlohmann::json::parse(raw);
        if (parsed.is_array()) {
            for (const auto& item : parsed) {
                if (item.is_string()) {
                    values.push_back(item.get<std::string>());
                }
            }
        }
    } catch (...) {
        // 脏数据按空数组处理：不能因档案字段异常让搜索整体失败。
    }
    return values;
}

nlohmann::json mealTypesJson(const std::string& raw) {
    nlohmann::json out = nlohmann::json::array();
    try {
        const nlohmann::json parsed = nlohmann::json::parse(raw);
        if (parsed.is_array()) {
            for (const auto& item : parsed) {
                if (item.is_string()) {
                    out.push_back(item.get<std::string>());
                }
            }
        }
    } catch (...) {
        // 保持空数组，避免返回非法 JSON。
    }
    return out;
}

bool hasMealType(const nlohmann::json& mealTypes, const std::string& mealType) {
    for (const auto& item : mealTypes) {
        if (item.is_string() && item.get<std::string>() == mealType) {
            return true;
        }
    }
    return false;
}

bool intersectsKeywords(const std::string& keywords,
                        const std::vector<std::string>& tags) {
    const std::vector<std::string> words = splitTokens(keywords);
    for (const auto& tag : tags) {
        for (const auto& word : words) {
            if (tag == word) {
                return true;
            }
        }
    }
    return false;
}

bool containsAny(const std::string& haystack,
                 const std::vector<std::string>& needles) {
    for (const auto& needle : needles) {
        if (!needle.empty() && haystack.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

nlohmann::json foodJson(const FoodRow& food) {
    nlohmann::json item;
    item["id"] = food.id;
    item["name"] = food.name;
    item["category"] = food.category;
    item["description"] = food.description;
    item["calories"] = jsonutil::number(food.calories);
    item["protein"] = jsonutil::number(food.protein);
    item["fiber"] = jsonutil::number(food.fiber);
    item["veggies"] = jsonutil::number(food.veggies);
    item["water"] = jsonutil::number(food.water);
    item["nature"] = food.nature;
    item["keywords"] = food.keywords;
    item["meal_types"] = mealTypesJson(food.mealTypes);
    item["price"] = food.price;
    item["available"] = food.available;
    item["image"] = food.image;
    return item;
}

// 用户存在性校验：与 S2 各接口保持同一错误映射。
ServiceResult checkUser(MySQLPool& pool, const std::string& userId) {
    if (!Auth::validUid(userId)) {
        return makeError(ServiceResult::Status::InvalidParams,
                         "invalid user id");
    }
    UserDao users(pool);
    const UserDao::Lookup lookup = users.findByAgentUid(userId);
    if (lookup == UserDao::Lookup::Unavailable) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }
    if (lookup == UserDao::Lookup::NotFound) {
        return makeError(ServiceResult::Status::UserNotFound,
                         "user not found");
    }
    return ServiceResult{};
}

struct Candidate {
    int hit = 0;
    FoodRow food;
};

}  // namespace

FoodService::FoodService(MySQLPool& pool, std::string defaultUserId)
    : m_pool(pool), m_defaultUserId(std::move(defaultUserId)) {}

ServiceResult FoodService::menu(const std::string& userId,
                                const std::string& mealType) const {
    if (!isKnownMealType(mealType)) {
        return makeError(ServiceResult::Status::InvalidParams,
                         "meal_type must be one of 早餐/午餐/晚餐/加餐");
    }
    ServiceResult userCheck = checkUser(m_pool, userId);
    if (userCheck.status != ServiceResult::Status::Ok) {
        return userCheck;
    }

    FoodDao foods(m_pool);
    std::vector<FoodRow> rows;
    if (foods.listAvailable(rows) != FoodDao::Lookup::Found) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }

    nlohmann::json list = nlohmann::json::array();
    for (const auto& row : rows) {
        const nlohmann::json types = mealTypesJson(row.mealTypes);
        if (!mealType.empty() && !hasMealType(types, mealType)) {
            continue;
        }
        list.push_back(foodJson(row));
    }

    ServiceResult result;
    result.status = ServiceResult::Status::Ok;
    result.data["user_id"] = userId;
    result.data["meal_type"] = mealType.empty() ? "全部" : mealType;
    result.data["foods"] = list;
    result.data["count"] = static_cast<int>(list.size());
    return result;
}

ServiceResult FoodService::search(
    const std::string& userId, const std::string& query,
    const std::vector<std::string>& tags, const std::string& mealType,
    int limit, const std::string& nature) const {
    if (!isKnownMealType(mealType)) {
        return makeError(ServiceResult::Status::InvalidParams,
                         "meal_type must be one of 早餐/午餐/晚餐/加餐");
    }
    if (!isKnownNature(nature)) {
        return makeError(ServiceResult::Status::InvalidParams,
                         "nature must be recommend or indulge");
    }
    const int effectiveLimit = limit <= 0 ? kDefaultLimit : limit;
    if (effectiveLimit > kMaxLimit) {
        return makeError(ServiceResult::Status::InvalidParams,
                         "limit must be in [1,16]");
    }
    if (query.size() > kMaxQueryLength) {
        return makeError(ServiceResult::Status::InvalidParams,
                         "query is too long");
    }
    if (tags.size() > kMaxTags) {
        return makeError(ServiceResult::Status::InvalidParams,
                         "too many tags");
    }
    for (const auto& tag : tags) {
        if (tag.size() > kMaxTagLength) {
            return makeError(ServiceResult::Status::InvalidParams,
                             "tag is too long");
        }
    }

    ServiceResult userCheck = checkUser(m_pool, userId);
    if (userCheck.status != ServiceResult::Status::Ok) {
        return userCheck;
    }

    // 过敏与忌口来自 health_profile；档案缺失时按空数组处理（与 mock 一致）。
    ProfileDao profiles(m_pool);
    HealthProfileRow profile;
    const ProfileDao::Lookup profileLookup = profiles.load(userId, profile);
    if (profileLookup == ProfileDao::Lookup::Unavailable) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }
    const std::vector<std::string> allergies = parseStringArray(profile.allergies);
    const std::vector<std::string> dislikes = parseStringArray(profile.dislikes);

    const std::vector<std::string> tokens = splitTokens(lowerAscii(query));

    FoodDao foods(m_pool);
    std::vector<FoodRow> rows;
    if (foods.listAvailable(rows) != FoodDao::Lookup::Found) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }

    std::vector<Candidate> candidates;
    for (const auto& row : rows) {
        const nlohmann::json types = mealTypesJson(row.mealTypes);
        if (!mealType.empty() && !hasMealType(types, mealType)) {
            continue;
        }
        if (!nature.empty() && row.nature != nature) {
            continue;
        }
        if (!tags.empty() && !intersectsKeywords(row.keywords, tags)) {
            continue;
        }
        const std::string haystack = lowerAscii(
            row.name + " " + row.category + " " + row.description + " " +
            row.keywords);
        int hit = 0;
        for (const auto& token : tokens) {
            if (haystack.find(token) != std::string::npos) {
                ++hit;
            }
        }
        if (!tokens.empty() && hit == 0) {
            continue;
        }
        if (containsAny(haystack, allergies)) {
            continue;
        }
        if (containsAny(haystack, dislikes)) {
            continue;
        }
        candidates.push_back(Candidate{hit, row});
    }

    // 与 mock 一致：命中词数降序，同分保持原顺序（stable_sort）。
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) {
                         return a.hit > b.hit;
                     });

    const std::size_t take = static_cast<std::size_t>(
        std::min<int>(effectiveLimit, static_cast<int>(candidates.size())));
    nlohmann::json list = nlohmann::json::array();
    for (std::size_t i = 0; i < take; ++i) {
        list.push_back(foodJson(candidates[i].food));
    }

    ServiceResult result;
    result.status = ServiceResult::Status::Ok;
    result.data["user_id"] = userId;
    result.data["foods"] = list;
    result.data["count"] = static_cast<int>(list.size());
    return result;
}