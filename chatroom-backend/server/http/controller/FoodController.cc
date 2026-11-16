#include "http/controller/FoodController.h"

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "http/Auth.h"
#include "http/HttpUtil.h"
#include "http/controller/ResultRenderer.h"

namespace {

std::vector<std::string> splitComma(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        if (c == ',') {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

}  // namespace

bool FoodController::resolveUser(const httplib::Request& req,
                                 std::string& userId,
                                 httplib::Response& res) const {
    const std::string raw = req.get_param_value("user_id");
    if (raw.empty()) {
        userId = m_service.defaultUserId();
        return true;
    }
    if (!Auth::validUid(raw)) {
        httpapi::invalidParams(res, "invalid user_id");
        return false;
    }
    if (!m_auth.allowUid(raw)) {
        httpapi::forbidden(res, "user_id is not allowed");
        return false;
    }
    userId = raw;
    return true;
}

void FoodController::menu(const httplib::Request& req, httplib::Response& res,
                          const Router::Captures&) {
    std::string userId;
    if (!resolveUser(req, userId, res)) {
        return;
    }
    const std::string mealType = req.get_param_value("meal_type");
    const ServiceResult result = m_service.menu(userId, mealType);
    if (result.status != ServiceResult::Status::Ok) {
        httpcontroller::renderError(res, result);
        return;
    }

    nlohmann::json payload = result.data;
    payload["source"] = "service";
    httpapi::ok(res, payload);
}

void FoodController::search(const httplib::Request& req, httplib::Response& res,
                            const Router::Captures&) {
    std::string userId;
    if (!resolveUser(req, userId, res)) {
        return;
    }

    int limit = 0;
    const std::string rawLimit = req.get_param_value("limit");
    if (!rawLimit.empty()) {
        if (!httpcontroller::parsePositiveInt(rawLimit, limit)) {
            httpapi::invalidParams(res, "limit must be an integer in [1,16]");
            return;
        }
    }

    // tags 支持重复参数与逗号分隔两种写法（Agent 当前不传，保留能力）。
    std::vector<std::string> tags;
    const auto range = req.params.equal_range("tags");
    for (auto it = range.first; it != range.second; ++it) {
        for (auto& tag : splitComma(it->second)) {
            tags.push_back(tag);
        }
    }

    const ServiceResult result = m_service.search(
        userId, req.get_param_value("q"), tags,
        req.get_param_value("meal_type"), limit,
        req.get_param_value("nature"));
    if (result.status != ServiceResult::Status::Ok) {
        httpcontroller::renderError(res, result);
        return;
    }

    nlohmann::json payload = result.data;
    payload["source"] = "service";
    httpapi::ok(res, payload);
}