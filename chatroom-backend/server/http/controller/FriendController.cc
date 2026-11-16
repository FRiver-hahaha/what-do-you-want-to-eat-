#include "http/controller/FriendController.h"

#include <nlohmann/json.hpp>

#include "http/HttpUtil.h"
#include "http/controller/ResultRenderer.h"

void FriendController::friends(const httplib::Request&,
                               httplib::Response& res,
                               const Router::Captures& captures) {
    const ServiceResult result = m_service.friends(captures.front());
    if (result.status != ServiceResult::Status::Ok) {
        httpcontroller::renderError(res, result);
        return;
    }

    nlohmann::json payload = result.data;
    payload["source"] = "service";
    httpapi::ok(res, payload);
}