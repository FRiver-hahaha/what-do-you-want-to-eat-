#include "http/controller/ProfileController.h"

#include <string>

#include <nlohmann/json.hpp>

#include "http/HttpUtil.h"

namespace {

void renderError(httplib::Response& res, const ServiceResult& result) {
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

}  // namespace

void ProfileController::get(const httplib::Request&, httplib::Response& res,
                            const Router::Captures& captures) {
    const ServiceResult result = m_service.getProfile(captures.front());
    if (result.status != ServiceResult::Status::Ok) {
        renderError(res, result);
        return;
    }
    // Agent 的 get_profile 直接把响应体当作档案对象使用，这里保持扁平结构。
    nlohmann::json body = result.data;
    body["source"] = "service";
    httpapi::setJson(res, 200, body);
}

void ProfileController::patch(const httplib::Request& req, httplib::Response& res,
                              const Router::Captures& captures) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        httpapi::invalidParams(res, "request body must be valid JSON");
        return;
    }
    if (!body.is_object()) {
        httpapi::invalidParams(res, "request body must be a JSON object");
        return;
    }

    const ServiceResult result = m_service.patchProfile(captures.front(), body);
    if (result.status != ServiceResult::Status::Ok) {
        renderError(res, result);
        return;
    }

    nlohmann::json payload = result.data;
    payload["source"] = "service";
    httpapi::ok(res, payload);
}