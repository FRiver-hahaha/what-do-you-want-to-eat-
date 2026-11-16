#include "http/controller/StatsController.h"

#include <string>

#include <nlohmann/json.hpp>

#include "http/HttpUtil.h"
#include "http/controller/ResultRenderer.h"

void StatsController::nutritionGap(const httplib::Request& req,
                                   httplib::Response& res,
                                   const Router::Captures& captures) {
    const std::string date = req.get_param_value("date");
    const ServiceResult result =
        m_service.nutritionGap(captures.front(), date);
    if (result.status != ServiceResult::Status::Ok) {
        httpcontroller::renderError(res, result);
        return;
    }

    nlohmann::json payload = result.data;
    payload["source"] = "service";
    httpapi::ok(res, payload);
}

void StatsController::weeklyStats(const httplib::Request&,
                                  httplib::Response& res,
                                  const Router::Captures& captures) {
    const ServiceResult result = m_service.weeklyStats(captures.front());
    if (result.status != ServiceResult::Status::Ok) {
        httpcontroller::renderError(res, result);
        return;
    }

    nlohmann::json payload = result.data;
    payload["source"] = "service";
    httpapi::ok(res, payload);
}