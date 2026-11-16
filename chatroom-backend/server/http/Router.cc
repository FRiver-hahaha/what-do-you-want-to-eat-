#include "http/Router.h"

#include "http/Auth.h"
#include "http/HttpUtil.h"

namespace {

bool isApiPath(const std::string& path) {
    static const std::string kApiPrefix = "/api/v1/";
    return path.size() >= kApiPrefix.size() &&
           path.compare(0, kApiPrefix.size(), kApiPrefix) == 0;
}

}  // namespace

void Router::add(const std::string& method, const std::string& pattern,
                 Handler handler) {
    m_routes.push_back(Route{method, pattern, std::move(handler)});
}

void Router::install(httplib::Server& server, const Auth& auth) const {
    for (const auto& route : m_routes) {
        const Handler handler = route.handler;
        auto wrapped = [&auth, handler](const httplib::Request& req,
                                        httplib::Response& res) {
            Captures captures;
            for (std::size_t i = 1; i < req.matches.size(); ++i) {
                captures.emplace_back(req.matches[i].str());
            }

            // 认证先于任何业务逻辑：未通过时不进入 Controller / DAO。
            if (isApiPath(req.path) && !auth.checkToken(req)) {
                httpapi::unauthorized(res, "missing or invalid bearer token");
                return;
            }
            if (!captures.empty()) {
                const std::string& uid = captures.front();
                if (!Auth::validUid(uid)) {
                    httpapi::invalidParams(res, "invalid user id");
                    return;
                }
                if (!auth.allowUid(uid)) {
                    httpapi::forbidden(res, "single-demo-user mode");
                    return;
                }
            }

            handler(req, res, captures);
        };

        if (route.method == "GET") {
            server.Get(route.pattern, wrapped);
        } else if (route.method == "POST") {
            server.Post(route.pattern, wrapped);
        } else if (route.method == "PUT") {
            server.Put(route.pattern, wrapped);
        } else if (route.method == "PATCH") {
            server.Patch(route.pattern, wrapped);
        } else if (route.method == "DELETE") {
            server.Delete(route.pattern, wrapped);
        }
    }
}