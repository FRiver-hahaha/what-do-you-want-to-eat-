#include "http/controller/RankingController.h"

#include <string>

#include <nlohmann/json.hpp>

#include "http/HttpUtil.h"
#include "http/controller/ResultRenderer.h"

void RankingController::friendRanking(const httplib::Request& req,
                                      httplib::Response& res,
                                      const Router::Captures& captures) {
    std::string scope = req.get_param_value("scope");
    if (scope.empty()) {
        scope = "week";
    }

    const ServiceResult result = m_service.friendRanking(captures.front(), scope);
    if (result.status != ServiceResult::Status::Ok) {
        httpcontroller::renderError(res, result);
        return;
    }

    nlohmann::json payload = result.data;
    payload["source"] = "service";
    httpapi::ok(res, payload);
}