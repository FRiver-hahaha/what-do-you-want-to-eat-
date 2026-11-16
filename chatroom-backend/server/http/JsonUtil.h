#pragma once

#include <cmath>

#include <nlohmann/json.hpp>

namespace jsonutil {

// 整数值统一输出为 JSON 整数：前端 meals 字段与 mock 契约都是整数形态。
inline nlohmann::json number(double value) {
    if (std::isfinite(value) && value == std::floor(value) &&
        std::fabs(value) < 1e15) {
        return nlohmann::json(static_cast<long long>(value));
    }
    return nlohmann::json(value);
}

}  // namespace jsonutil