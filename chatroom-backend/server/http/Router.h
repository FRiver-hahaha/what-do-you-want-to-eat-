#pragma once

#include <functional>
#include <string>
#include <vector>

#include "httplib.h"

class Auth;

// 路由表：把业务 Handler 绑定到 httplib，并在进入 Handler 前完成
// Token → UID 形状 → 演示白名单 校验。
// 不使用 pre_routing_handler：httplib 0.50 在路由匹配后才读取请求体，
// 在 pre-routing 阶段读 req.body 会拿到空串。
class Router {
   public:
    using Captures = std::vector<std::string>;
    using Handler = std::function<void(const httplib::Request&, httplib::Response&,
                                       const Captures&)>;

    void add(const std::string& method, const std::string& pattern, Handler handler);
    void install(httplib::Server& server, const Auth& auth) const;

   private:
    struct Route {
        std::string method;
        std::string pattern;
        Handler handler;
    };
    std::vector<Route> m_routes;
};