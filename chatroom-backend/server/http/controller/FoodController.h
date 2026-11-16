#pragma once

#include <string>

#include "http/Router.h"
#include "http/service/FoodService.h"
#include "httplib.h"

class Auth;

class FoodController {
   public:
    FoodController(FoodService& service, const Auth& auth)
        : m_service(service), m_auth(auth) {}

    void menu(const httplib::Request& req, httplib::Response& res,
              const Router::Captures& captures);
    void search(const httplib::Request& req, httplib::Response& res,
                const Router::Captures& captures);

   private:
    // Agent 的 /api/v1/menu 与 /api/v1/foods/search 不带 user_id，
    // 这里允许可选的 user_id 覆盖，但仍强制 UID 形状与演示白名单。
    bool resolveUser(const httplib::Request& req, std::string& userId,
                     httplib::Response& res) const;

    FoodService& m_service;
    const Auth& m_auth;
};