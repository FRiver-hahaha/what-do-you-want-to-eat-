#pragma once

#include "http/HttpUtil.h"
#include "http/service/ServiceResult.h"
#include "httplib.h"

namespace httpcontroller {

// Service 状态 → HTTP 200 + {ok:false,error,message}。
// 业务错误绝不能返回 4xx/5xx：Agent 会因此整段回退 mock。
inline void renderError(httplib::Response& res, const ServiceResult& result) {
    switch (result.status) {
        case ServiceResult::Status::UserNotFound:
            httpapi::fail(res, "user_not_found", "user not found");
            break;
        case ServiceResult::Status::InvalidParams:
            httpapi::invalidParams(res, result.message);
            break;
        case ServiceResult::Status::Unavailable:
            httpapi::fail(res, "service_unavailable", "database unavailable");
            break;
        case ServiceResult::Status::Conflict:
            httpapi::fail(res, "idempotency_conflict", result.message);
            break;
        case ServiceResult::Status::Ok:
            break;
    }
}

// 解析并校验路径/查询参数中的正整数；返回 false 表示参数非法。
inline bool parsePositiveInt(const std::string& text, int& out) {
    if (text.empty() || text.size() > 6) {
        return false;
    }
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

}  // namespace httpcontroller