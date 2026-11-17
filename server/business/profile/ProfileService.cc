#include "server/business/profile/ProfileService.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace eat::business {
namespace {

double Round2(double value) { return std::round(value * 100.0) / 100.0; }

std::string FormatSleep(double hours) {
    if (hours == std::floor(hours)) {
        return std::to_string(static_cast<long long>(hours)) + "小时";
    }
    std::string text = std::to_string(hours);
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text + "小时";
}

protocol::JsonValue ParseArray(const std::string& raw) {
    if (!raw.empty()) {
        std::string reason;
        protocol::JsonValue parsed = protocol::JsonValue::Parse(raw, reason);
        if (reason.empty() && parsed.IsArray()) return parsed;
    }
    return protocol::JsonValue::Array();
}

bool ReadString(const protocol::JsonValue* value, std::string& out) {
    if (value == nullptr || !value->IsString()) return false;
    out = value->AsString();
    return true;
}

bool ReadNumber(const protocol::JsonValue* value, double& out) {
    if (value == nullptr) return false;
    if (value->IsInt()) {
        out = static_cast<double>(value->AsInt());
        return true;
    }
    if (value->IsDouble()) {
        out = value->AsDouble();
        return true;
    }
    return false;
}

bool ReadInt(const protocol::JsonValue* value, int& out) {
    if (value == nullptr || !value->IsInt()) return false;
    out = static_cast<int>(value->AsInt());
    return true;
}

std::string Utf8Truncate(const std::string& input, std::size_t max_chars) {
    std::size_t index = 0;
    std::size_t count = 0;
    while (index < input.size() && count < max_chars) {
        const unsigned char c = static_cast<unsigned char>(input[index]);
        std::size_t len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (index + len > input.size()) break;
        index += len;
        ++count;
    }
    return input.substr(0, index);
}

protocol::JsonValue BuildProfile(const std::string& user_key, const HealthProfileRow& row) {
    protocol::JsonValue out = protocol::JsonValue::Object();
    out.Set("user_id", protocol::JsonValue::String(user_key));
    out.Set("name", protocol::JsonValue::String(row.name));
    out.Set("gender", protocol::JsonValue::String(row.gender));
    out.Set("age", protocol::JsonValue::Int(row.age));
    out.Set("height", protocol::JsonValue::Double(row.height));
    out.Set("weight", protocol::JsonValue::Double(row.weight));
    double bmi = 0.0;
    if (row.height > 0) {
        const double meters = row.height / 100.0;
        bmi = Round2(row.weight / (meters * meters));
    }
    out.Set("bmi", protocol::JsonValue::Double(bmi));
    out.Set("goal", protocol::JsonValue::String(row.goal));
    out.Set("activity", protocol::JsonValue::String(row.activity));
    out.Set("diet_style", protocol::JsonValue::String(row.diet_style));
    out.Set("water_target", protocol::JsonValue::Int(row.water_target));
    out.Set("waterTarget", protocol::JsonValue::Int(row.water_target));
    out.Set("sleep", protocol::JsonValue::String(FormatSleep(row.sleep_hours)));
    out.Set("sleep_hours", protocol::JsonValue::Double(row.sleep_hours));
    out.Set("allergies", ParseArray(row.allergies));
    out.Set("dislikes", ParseArray(row.dislikes));
    out.Set("spend_min", protocol::JsonValue::Int(row.spend_min));
    out.Set("spend_max", protocol::JsonValue::Int(row.spend_max));
    out.Set("note", protocol::JsonValue::String(row.note));
    out.Set("source", protocol::JsonValue::String("service"));
    return out;
}

ProfileResult Unavailable() {
    ProfileResult result;
    result.error = protocol::ErrorCode::ServiceUnavailable;
    result.message = "database unavailable";
    return result;
}

ProfileResult UserNotFound() {
    ProfileResult result;
    result.error = protocol::ErrorCode::UserNotFound;
    result.message = "user not found";
    return result;
}

}  // namespace

ProfileResult ProfileService::Get(const std::string& user_key) {
    if (user_key.empty()) return UserNotFound();
    if (!identity_.UserExists(user_key)) return UserNotFound();

    ProfileDao dao(pool_);
    HealthProfileRow row;
    const ProfileDao::Lookup lookup = dao.Load(user_key, row);
    if (lookup == ProfileDao::Lookup::Unavailable) return Unavailable();

    ProfileResult result;
    result.ok = true;
    result.data = BuildProfile(user_key, row);
    return result;
}

ProfileResult ProfileService::Patch(const std::string& user_key,
                                    const protocol::JsonValue& body) {
    if (user_key.empty()) return UserNotFound();
    if (!body.IsObject() || body.AsObject().empty()) {
        ProfileResult result;
        result.error = protocol::ErrorCode::InvalidParams;
        result.message = "params must be a non-empty object";
        return result;
    }
    if (!identity_.UserExists(user_key)) return UserNotFound();

    ProfileDao dao(pool_);
    HealthProfileRow row;
    const ProfileDao::Lookup lookup = dao.Load(user_key, row);
    if (lookup == ProfileDao::Lookup::Unavailable) return Unavailable();

    std::string text;
    double number = 0.0;
    for (const auto& item : body.AsObject()) {
        const std::string& key = item.first;
        const protocol::JsonValue* value = &item.second;
        if (key == "name" && ReadString(value, text)) row.name = Utf8Truncate(text, 20);
        else if (key == "gender" && ReadString(value, text)) row.gender = Utf8Truncate(text, 20);
        else if (key == "goal" && ReadString(value, text)) row.goal = Utf8Truncate(text, 120);
        else if (key == "activity" && ReadString(value, text)) row.activity = Utf8Truncate(text, 120);
        else if (key == "diet_style" && ReadString(value, text)) row.diet_style = Utf8Truncate(text, 20);
        else if (key == "note" && ReadString(value, text)) row.note = Utf8Truncate(text, 120);
        else if ((key == "water_target" || key == "waterTarget") && ReadInt(value, row.water_target)) {}
        else if (key == "age" && ReadInt(value, row.age)) {}
        else if (key == "spend_min" && ReadInt(value, row.spend_min)) {}
        else if (key == "spend_max" && ReadInt(value, row.spend_max)) {}
        else if (key == "height" && ReadNumber(value, number) && number > 0 && number <= 500) row.height = number;
        else if (key == "weight" && ReadNumber(value, number) && number > 0 && number <= 500) row.weight = number;
        else if ((key == "sleep" || key == "sleep_hours") && ReadNumber(value, number) &&
                 number >= 0 && number <= 24) row.sleep_hours = number;
    }
    if (row.spend_min > row.spend_max) std::swap(row.spend_min, row.spend_max);
    if (!dao.Save(user_key, row)) return Unavailable();

    ProfileResult result;
    result.ok = true;
    result.data = BuildProfile(user_key, row);
    return result;
}

}  // namespace eat::business