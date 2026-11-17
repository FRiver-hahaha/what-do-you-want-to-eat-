#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "shared/protocol/JsonValue.h"

namespace eat::business {

inline protocol::JsonValue JsonNumber(double value) {
    if (std::isfinite(value) && value == std::floor(value) && std::fabs(value) < 1e15) {
        return protocol::JsonValue::Int(static_cast<std::int64_t>(value));
    }
    return protocol::JsonValue::Double(value);
}

inline protocol::JsonValue ParseStringArray(const std::string& raw) {
    protocol::JsonValue out = protocol::JsonValue::Array();
    if (raw.empty()) return out;
    std::string reason;
    protocol::JsonValue parsed = protocol::JsonValue::Parse(raw, reason);
    if (!reason.empty() || !parsed.IsArray()) return out;
    for (const auto& item : parsed.AsArray()) {
        if (item.IsString()) out.AsArray().push_back(item);
    }
    return out;
}

inline std::vector<std::string> ReadStringArray(const protocol::JsonValue& value) {
    std::vector<std::string> out;
    if (!value.IsArray()) return out;
    for (const auto& item : value.AsArray()) {
        if (item.IsString()) out.push_back(item.AsString());
    }
    return out;
}

inline std::vector<std::string> ParseStringArrayText(const std::string& raw) {
    std::vector<std::string> out;
    const protocol::JsonValue parsed = ParseStringArray(raw);
    for (const auto& item : parsed.AsArray()) out.push_back(item.AsString());
    return out;
}

}  // namespace eat::business
