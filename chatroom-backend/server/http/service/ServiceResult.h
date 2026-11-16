#pragma once

#include <string>

#include <nlohmann/json.hpp>

// Service 层统一结果：Controller 只按 status 映射到 HTTP 200 + ok:false 错误体。
struct ServiceResult {
    enum class Status {
        Ok,
        UserNotFound,
        InvalidParams,
        Unavailable,
        // 幂等键已存在但 payload 不一致：拒绝本次写入，不改动任何数据。
        Conflict,
    };

    Status status = Status::Ok;
    nlohmann::json data;
    std::string message;
};