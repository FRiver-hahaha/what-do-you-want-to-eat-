#include "http/service/ProfileService.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "database/MySQLPool.h"
#include "http/dao/ProfileDao.h"
#include "http/dao/UserDao.h"

namespace {

constexpr double kBodyMetricMax = 500.0;
constexpr int kIntFieldMax = 9999;
constexpr double kSleepHoursMax = 24.0;
constexpr std::size_t kMaxArrayItems = 20;
constexpr std::size_t kMaxArrayItemChars = 30;

HealthProfileRow defaultRow() {
    HealthProfileRow row;
    row.name = "我";
    row.gender = "女";
    row.age = 28;
    row.height = 170;
    row.weight = 60;
    row.goal = "均衡饮食";
    row.activity = "久坐";
    row.dietStyle = "普通饮食";
    row.waterTarget = 1800;
    row.sleepHours = 7;
    row.allergies = "[\"花生\"]";
    row.dislikes = "[\"香菜\"]";
    row.spendMin = 20;
    row.spendMax = 35;
    row.note = "下午容易口渴，晚餐不宜过饱";
    return row;
}

std::string utf8Truncate(const std::string& input, std::size_t maxChars) {
    std::size_t index = 0;
    std::size_t count = 0;
    while (index < input.size() && count < maxChars) {
        const unsigned char c = static_cast<unsigned char>(input[index]);
        std::size_t len = 1;
        if ((c & 0x80) == 0) {
            len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
        }
        if (index + len > input.size()) {
            break;
        }
        index += len;
        ++count;
    }
    return input.substr(0, index);
}

bool readNumber(const nlohmann::json& value, double& out) {
    if (value.is_number()) {
        out = value.get<double>();
        return std::isfinite(out);
    }
    if (value.is_string()) {
        const std::string text = value.get<std::string>();
        try {
            std::size_t pos = 0;
            const double parsed = std::stod(text, &pos);
            if (pos == text.size() && std::isfinite(parsed)) {
                out = parsed;
                return true;
            }
        } catch (...) {
        }
    }
    return false;
}

bool readInt(const nlohmann::json& value, int& out) {
    double parsed = 0;
    if (!readNumber(value, parsed)) {
        return false;
    }
    if (parsed != std::floor(parsed)) {
        return false;
    }
    out = static_cast<int>(parsed);
    return true;
}

bool readSleepHours(const nlohmann::json& value, double& out) {
    if (value.is_number()) {
        out = value.get<double>();
        return std::isfinite(out);
    }
    if (!value.is_string()) {
        return false;
    }
    std::string text = value.get<std::string>();
    static const std::string kSuffix = "小时";
    if (text.size() >= kSuffix.size() &&
        text.compare(text.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0) {
        text.erase(text.size() - kSuffix.size());
    } else if (!text.empty() && (text.back() == 'h' || text.back() == 'H')) {
        text.pop_back();
    }
    try {
        std::size_t pos = 0;
        const double parsed = std::stod(text, &pos);
        if (pos == text.size() && std::isfinite(parsed)) {
            out = parsed;
            return true;
        }
    } catch (...) {
    }
    return false;
}

std::string formatSleep(double hours) {
    if (hours == std::floor(hours)) {
        return std::to_string(static_cast<long long>(hours)) + "小时";
    }
    std::string text = std::to_string(hours);
    while (!text.empty() && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text + "小时";
}

nlohmann::json parseArray(const std::string& raw) {
    if (raw.empty()) {
        return nlohmann::json::array();
    }
    try {
        const nlohmann::json parsed = nlohmann::json::parse(raw);
        if (parsed.is_array()) {
            return parsed;
        }
    } catch (...) {
    }
    return nlohmann::json::array();
}

nlohmann::json buildProfile(const std::string& agentUid,
                            const HealthProfileRow& row) {
    nlohmann::json profile;
    profile["user_id"] = agentUid;
    profile["name"] = row.name;
    profile["gender"] = row.gender;
    profile["age"] = row.age;
    profile["height"] = row.height;
    profile["weight"] = row.weight;
    if (row.height > 0) {
        const double meters = row.height / 100.0;
        profile["bmi"] = std::round(row.weight / (meters * meters) * 100.0) / 100.0;
    } else {
        profile["bmi"] = 0.0;
    }
    profile["goal"] = row.goal;
    profile["activity"] = row.activity;
    profile["diet_style"] = row.dietStyle;
    profile["water_target"] = row.waterTarget;
    profile["waterTarget"] = row.waterTarget;
    profile["sleep"] = formatSleep(row.sleepHours);
    profile["sleep_hours"] = row.sleepHours;
    profile["allergies"] = parseArray(row.allergies);
    profile["dislikes"] = parseArray(row.dislikes);
    profile["spend_min"] = row.spendMin;
    profile["spend_max"] = row.spendMax;
    profile["note"] = row.note;
    return profile;
}

struct ApplyResult {
    bool valid = true;
    std::string message;
    std::vector<std::string> updated;
};

bool readText(const nlohmann::json& value, std::size_t maxChars, std::string& out,
              ApplyResult& result, const char* field) {
    if (!value.is_string()) {
        result.valid = false;
        result.message = std::string(field) + " must be a string";
        return false;
    }
    out = utf8Truncate(value.get<std::string>(), maxChars);
    return true;
}

bool readMetric(const nlohmann::json& value, double& out, ApplyResult& result,
                const char* field) {
    if (!readNumber(value, out) || !(out > 0.0) || out > kBodyMetricMax) {
        result.valid = false;
        result.message = std::string(field) + " must be in (0, 500]";
        return false;
    }
    return true;
}

bool readBoundedInt(const nlohmann::json& value, int& out, ApplyResult& result,
                    const char* field) {
    if (!readInt(value, out) || out < 0 || out > kIntFieldMax) {
        result.valid = false;
        result.message = std::string(field) + " must be an integer in [0, 9999]";
        return false;
    }
    return true;
}

bool readStringArray(const nlohmann::json& value, std::string& out,
                     ApplyResult& result, const char* field) {
    if (!value.is_array()) {
        result.valid = false;
        result.message = std::string(field) + " must be an array of strings";
        return false;
    }
    nlohmann::json items = nlohmann::json::array();
    std::size_t count = 0;
    for (const auto& item : value) {
        if (!item.is_string()) {
            result.valid = false;
            result.message = std::string(field) + " must be an array of strings";
            return false;
        }
        if (count >= kMaxArrayItems) {
            break;
        }
        items.push_back(utf8Truncate(item.get<std::string>(), kMaxArrayItemChars));
        ++count;
    }
    out = items.dump();
    return true;
}

ApplyResult applyFields(const nlohmann::json& body, HealthProfileRow& row) {
    ApplyResult result;
    for (auto it = body.begin(); it != body.end(); ++it) {
        const std::string& key = it.key();
        const nlohmann::json& value = it.value();

        if (key == "name") {
            if (!readText(value, 20, row.name, result, "name")) return result;
        } else if (key == "gender") {
            if (!readText(value, 20, row.gender, result, "gender")) return result;
        } else if (key == "goal") {
            if (!readText(value, 120, row.goal, result, "goal")) return result;
        } else if (key == "activity") {
            if (!readText(value, 120, row.activity, result, "activity")) return result;
        } else if (key == "diet_style") {
            if (!readText(value, 20, row.dietStyle, result, "diet_style")) return result;
        } else if (key == "note") {
            if (!readText(value, 120, row.note, result, "note")) return result;
        } else if (key == "age") {
            if (!readBoundedInt(value, row.age, result, "age")) return result;
        } else if (key == "water_target" || key == "waterTarget") {
            if (!readBoundedInt(value, row.waterTarget, result, key.c_str())) return result;
        } else if (key == "sleep" || key == "sleep_hours") {
            if (!readSleepHours(value, row.sleepHours) || row.sleepHours < 0 ||
                row.sleepHours > kSleepHoursMax) {
                result.valid = false;
                result.message = key + " must be hours in [0, 24]";
                return result;
            }
        } else if (key == "height") {
            if (!readMetric(value, row.height, result, "height")) return result;
        } else if (key == "weight") {
            if (!readMetric(value, row.weight, result, "weight")) return result;
        } else if (key == "spend_min") {
            if (!readBoundedInt(value, row.spendMin, result, "spend_min")) return result;
        } else if (key == "spend_max") {
            if (!readBoundedInt(value, row.spendMax, result, "spend_max")) return result;
        } else if (key == "allergies") {
            if (!readStringArray(value, row.allergies, result, "allergies")) return result;
        } else if (key == "dislikes") {
            if (!readStringArray(value, row.dislikes, result, "dislikes")) return result;
        } else {
            continue;
        }
        result.updated.push_back(key);
    }

    if (row.spendMin > row.spendMax) {
        std::swap(row.spendMin, row.spendMax);
    }
    std::sort(result.updated.begin(), result.updated.end());
    result.updated.erase(std::unique(result.updated.begin(), result.updated.end()),
                         result.updated.end());
    return result;
}

ServiceResult unavailable() {
    ServiceResult result;
    result.status = ServiceResult::Status::Unavailable;
    result.message = "database unavailable";
    return result;
}

}  // namespace

ServiceResult ProfileService::getProfile(const std::string& agentUid) const {
    UserDao users(m_pool);
    const UserDao::Lookup userLookup = users.findByAgentUid(agentUid);
    if (userLookup == UserDao::Lookup::Unavailable) {
        return unavailable();
    }
    if (userLookup == UserDao::Lookup::NotFound) {
        ServiceResult result;
        result.status = ServiceResult::Status::UserNotFound;
        result.message = "user not found";
        return result;
    }

    ProfileDao profiles(m_pool);
    HealthProfileRow row = defaultRow();
    const ProfileDao::Lookup profileLookup = profiles.load(agentUid, row);
    if (profileLookup == ProfileDao::Lookup::Unavailable) {
        return unavailable();
    }

    ServiceResult result;
    result.status = ServiceResult::Status::Ok;
    result.data = buildProfile(agentUid, row);
    return result;
}

ServiceResult ProfileService::patchProfile(const std::string& agentUid,
                                           const nlohmann::json& body) const {
    if (!body.is_object() || body.empty()) {
        ServiceResult result;
        result.status = ServiceResult::Status::InvalidParams;
        result.message = "request body must be a non-empty JSON object";
        return result;
    }

    UserDao users(m_pool);
    const UserDao::Lookup userLookup = users.findByAgentUid(agentUid);
    if (userLookup == UserDao::Lookup::Unavailable) {
        return unavailable();
    }
    if (userLookup == UserDao::Lookup::NotFound) {
        ServiceResult result;
        result.status = ServiceResult::Status::UserNotFound;
        result.message = "user not found";
        return result;
    }

    ProfileDao profiles(m_pool);
    HealthProfileRow row = defaultRow();
    const ProfileDao::Lookup profileLookup = profiles.load(agentUid, row);
    if (profileLookup == ProfileDao::Lookup::Unavailable) {
        return unavailable();
    }

    const ApplyResult applied = applyFields(body, row);
    if (!applied.valid) {
        ServiceResult result;
        result.status = ServiceResult::Status::InvalidParams;
        result.message = applied.message;
        return result;
    }

    if (!profiles.save(agentUid, row)) {
        return unavailable();
    }

    ServiceResult result;
    result.status = ServiceResult::Status::Ok;
    result.data = buildProfile(agentUid, row);
    result.data["updated"] = applied.updated;
    return result;
}