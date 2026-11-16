#include "http/controller/MealController.h"

#include <string>

#include <nlohmann/json.hpp>

#include "http/HttpUtil.h"
#include "http/controller/ResultRenderer.h"

void MealController::record(const httplib::Request& req, httplib::Response& res,
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

    const ServiceResult result = m_service.recordMeal(captures.front(), body);
    if (result.status != ServiceResult::Status::Ok) {
        httpcontroller::renderError(res, result);
        return;
    }

    nlohmann::json payload = result.data;
    payload["source"] = "service";
    httpapi::ok(res, payload);
}

void MealController::recent(const httplib::Request& req, httplib::Response& res,
                            const Router::Captures& captures) {
    int days = 7;
    const std::string raw = req.get_param_value("days");
    if (!raw.empty() && !httpcontroller::parsePositiveInt(raw, days)) {
        httpapi::invalidParams(res, "days must be an integer in [1, 90]");
        return;
    }

    const ServiceResult result = m_service.recentMeals(captures.front(), days);
    if (result.status != ServiceResult::Status::Ok) {
        httpcontroller::renderError(res, result);
        return;
    }

    nlohmann::json payload = result.data;
    payload["source"] = "service";
    httpapi::ok(res, payload);
}