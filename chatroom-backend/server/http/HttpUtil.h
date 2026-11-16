#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "httplib.h"

namespace httpapi {

// 业务错误一律 HTTP 200 + {ok:false,error,message}：
// Agent 的 ServiceClient 遇到非 2xx 会整段回退 mock，4xx/5xx 会掩盖真实错误。
inline void setJson(httplib::Response& res, int status, const nlohmann::json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

inline void ok(httplib::Response& res, nlohmann::json body) {
    body["ok"] = true;
    setJson(res, 200, body);
}

inline void fail(httplib::Response& res, const std::string& error,
                 const std::string& message) {
    nlohmann::json body;
    body["ok"] = false;
    body["error"] = error;
    body["message"] = message;
    setJson(res, 200, body);
}

inline void unauthorized(httplib::Response& res, const std::string& message) {
    fail(res, "unauthorized", message);
}

inline void forbidden(httplib::Response& res, const std::string& message) {
    fail(res, "forbidden", message);
}

inline void invalidParams(httplib::Response& res, const std::string& message) {
    fail(res, "invalid_params", message);
}

inline void notFound(httplib::Response& res, const std::string& message) {
    fail(res, "not_found", message);
}

inline void internalError(httplib::Response& res, const std::string& message) {
    fail(res, "internal_error", message);
}

}  // namespace httpapi