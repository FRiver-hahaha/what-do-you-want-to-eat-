#include <glog/logging.h>
#include <mysql/mysql.h>

#include <cstdlib>
#include <exception>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "common/Config.h"
#include "database/MySQLPool.h"
#include "http/Auth.h"
#include "http/DbConn.h"
#include "http/HttpUtil.h"
#include "http/Router.h"
#include "http/controller/FoodController.h"
#include "http/controller/FriendController.h"
#include "http/controller/MealController.h"
#include "http/controller/ProfileController.h"
#include "http/controller/RankingController.h"
#include "http/controller/StatsController.h"
#include "http/service/FoodService.h"
#include "http/service/FriendService.h"
#include "http/service/MealService.h"
#include "http/service/ProfileService.h"
#include "http/service/RankingService.h"
#include "http/service/StatsService.h"
#include "httplib.h"

namespace {

int pickWorkers(int configured) {
    if (configured > 0) {
        return configured;
    }
    const unsigned hw = std::thread::hardware_concurrency();
    const int base = hw > 1 ? static_cast<int>(hw) - 1 : 1;
    return base < 8 ? 8 : base;
}

long long scalar(MYSQL* conn, const std::string& sql, bool& ok) {
    ok = false;
    if (mysql_query(conn, sql.c_str()) != 0) {
        return -1;
    }
    MYSQL_RES* result = mysql_store_result(conn);
    if (result == nullptr) {
        return -1;
    }
    MYSQL_ROW row = mysql_fetch_row(result);
    long long value = -1;
    if (row != nullptr && row[0] != nullptr) {
        value = std::atoll(row[0]);
        ok = true;
    }
    mysql_free_result(result);
    return value;
}

// 迁移未执行时拒绝启动：避免在缺列/缺表的库上返回错误的业务结果。
bool schemaReady(MySQLPool& pool) {
    DbConn conn(pool, 3000);
    if (!conn) {
        return false;
    }
    bool ok = false;
    const long long hasColumn = scalar(
        conn.get(),
        "SELECT COUNT(*) FROM information_schema.COLUMNS "
        "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='user' "
        "AND COLUMN_NAME='agent_uid'",
        ok);
    if (!ok || hasColumn != 1) {
        return false;
    }
    const long long tables = scalar(
        conn.get(),
        "SELECT COUNT(*) FROM information_schema.TABLES "
        "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN "
        "('health_profile','meal_records','idempotency_records','foods')",
        ok);
    if (!ok || tables != 4) {
        return false;
    }
    // S2 幂等回放依赖 meal_id：缺列时直接拒绝启动，而不是返回错误的幂等结果。
    const long long hasMealId = scalar(
        conn.get(),
        "SELECT COUNT(*) FROM information_schema.COLUMNS "
        "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='idempotency_records' "
        "AND COLUMN_NAME='meal_id'",
        ok);
    return ok && hasMealId == 1;
}

void registerRoutes(Router& router, ProfileController& profile,
                    MealController& meal, StatsController& stats,
                    FoodController& food, RankingController& ranking,
                    FriendController& friendCtl, MySQLPool& pool) {
    router.add("GET", "^/health$",
               [&pool](const httplib::Request&, httplib::Response& res,
                       const Router::Captures&) {
                   bool dbOk = false;
                   {
                       DbConn conn(pool, 1000);
                       dbOk = static_cast<bool>(conn) &&
                              mysql_ping(conn.get()) == 0;
                   }
                   nlohmann::json body;
                   body["status"] = "ok";
                   body["service"] = "chatroom-http";
                    body["version"] = "p8-s4";
                   body["database"] = dbOk ? "ok" : "unavailable";
                   httpapi::ok(res, body);
               });

    // 路径捕获保持宽松，UID 形状校验统一由 Auth::validUid 完成：
    // 非法 UID 必须返回 invalid_params，而不是 route not_found。
    const std::string profilePattern =
        "^/api/v1/users/([^/]+)/health-profile$";
    router.add("GET", profilePattern,
               [&profile](const httplib::Request& req, httplib::Response& res,
                          const Router::Captures& captures) {
                   profile.get(req, res, captures);
               });
    router.add("PATCH", profilePattern,
               [&profile](const httplib::Request& req, httplib::Response& res,
                          const Router::Captures& captures) {
                   profile.patch(req, res, captures);
               });

    const std::string mealsPattern = "^/api/v1/users/([^/]+)/meals$";
    router.add("POST", mealsPattern,
               [&meal](const httplib::Request& req, httplib::Response& res,
                       const Router::Captures& captures) {
                   meal.record(req, res, captures);
               });
    router.add("GET", mealsPattern,
               [&meal](const httplib::Request& req, httplib::Response& res,
                       const Router::Captures& captures) {
                   meal.recent(req, res, captures);
               });
    router.add("GET", "^/api/v1/users/([^/]+)/nutrition-gap$",
               [&stats](const httplib::Request& req, httplib::Response& res,
                        const Router::Captures& captures) {
                   stats.nutritionGap(req, res, captures);
               });
    router.add("GET", "^/api/v1/users/([^/]+)/weekly-stats$",
               [&stats](const httplib::Request& req, httplib::Response& res,
                        const Router::Captures& captures) {
                   stats.weeklyStats(req, res, captures);
               });

    // S3：菜单、菜品搜索、好友排行榜。
    // 前两者在 Agent 契约里不带 user_id，由 FoodController 解析默认用户。
    router.add("GET", "^/api/v1/menu$",
               [&food](const httplib::Request& req, httplib::Response& res,
                       const Router::Captures& captures) {
                   food.menu(req, res, captures);
               });
    router.add("GET", "^/api/v1/foods/search$",
               [&food](const httplib::Request& req, httplib::Response& res,
                       const Router::Captures& captures) {
                   food.search(req, res, captures);
               });
    router.add("GET", "^/api/v1/users/([^/]+)/ranking$",
               [&ranking](const httplib::Request& req, httplib::Response& res,
                          const Router::Captures& captures) {
                   ranking.friendRanking(req, res, captures);
               });

    // S4：好友列表。Agent 的 market.share() 依赖 friends[].id 校验好友关系。
    router.add("GET", "^/api/v1/users/([^/]+)/friends$",
               [&friendCtl](const httplib::Request& req, httplib::Response& res,
                         const Router::Captures& captures) {
                   friendCtl.friends(req, res, captures);
               });
}

}  // namespace

int main(int argc, char** argv) {
    google::InitGoogleLogging(argc > 0 ? argv[0] : "server_http");
    FLAGS_logtostderr = true;
    FLAGS_minloglevel = envInt("GLOG_minloglevel", 0);

    const std::string bindAddr = envOr("CHATROOM_HTTP_BIND", "127.0.0.1");
    const int port = envInt("CHATROOM_HTTP_PORT", 8080);
    const std::string token = envOr("CHATROOM_HTTP_TOKEN", "");
    const std::string userMode = envOr("CHATROOM_HTTP_USER_MODE", "p4_demo");

    Auth auth;
    if (!auth.init(token, userMode)) {
        if (token.empty()) {
            LOG(FATAL) << "CHATROOM_HTTP_TOKEN is required (fail-closed)";
        } else {
            LOG(FATAL) << "unsupported CHATROOM_HTTP_USER_MODE=" << userMode
                       << " (fail-closed)";
        }
    }

    const int workers = pickWorkers(envInt("CHATROOM_HTTP_WORKERS", 0));
    int poolSize = envInt("CHATROOM_HTTP_DB_POOL", 0);
    if (poolSize <= 0) {
        poolSize = workers;
    }

    const std::string mysqlHost = envOr("CHATROOM_MYSQL_HOST", "127.0.0.1");
    const int mysqlPort = envInt("CHATROOM_MYSQL_PORT", 3306);
    const std::string mysqlUser = envOr("CHATROOM_MYSQL_USER", "chatserver");
    const std::string mysqlPass = envOr("CHATROOM_MYSQL_PASSWORD", "");
    const std::string mysqlDb = envOr("CHATROOM_MYSQL_DB", "chatroom_p8");

    MySQLPool pool;
    if (!pool.init(poolSize, mysqlUser, mysqlPass, mysqlDb, mysqlHost,
                   static_cast<unsigned short>(mysqlPort))) {
        LOG(FATAL) << "database pool init failed (fail-closed): db=" << mysqlDb;
    }
    if (!schemaReady(pool)) {
        LOG(FATAL) << "schema check failed: apply migration 001_p8_business first "
                      "(fail-closed): db=" << mysqlDb;
    }

    ProfileService profileService(pool);
    ProfileController profileController(profileService);
    MealService mealService(pool);
    MealController mealController(mealService);
    StatsService statsService(pool);
    StatsController statsController(statsService);
    // Agent 的菜单/搜索接口不带 user_id，用该用户加载过敏与忌口。
    const std::string foodUser = envOr("CHATROOM_HTTP_FOOD_USER", "u_demo");
    if (!auth.allowUid(foodUser)) {
        LOG(FATAL) << "CHATROOM_HTTP_FOOD_USER is not allowed in "
                   << auth.mode() << " mode (fail-closed): " << foodUser;
    }
    FoodService foodService(pool, foodUser);
    FoodController foodController(foodService, auth);
    RankingService rankingService(pool);
    RankingController rankingController(rankingService);
    FriendService friendService(pool);
    FriendController friendController(friendService);

    Router router;
    registerRoutes(router, profileController, mealController, statsController,
                   foodController, rankingController, friendController, pool);

    httplib::Server svr;
    svr.new_task_queue = [workers] {
        return new httplib::ThreadPool(static_cast<std::size_t>(workers));
    };

    svr.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.body.empty()) {
            httpapi::notFound(res, "route not found");
        }
    });
    svr.set_exception_handler([](const httplib::Request&, httplib::Response& res,
                                 std::exception_ptr) {
        httpapi::internalError(res, "internal error");
    });

    router.install(svr, auth);

    LOG(INFO) << "chatroom-http listening on " << bindAddr << ":" << port
              << " mode=" << auth.mode() << " workers=" << workers
              << " db_pool=" << poolSize << " db=" << mysqlDb;
    if (!svr.listen(bindAddr, port)) {
        LOG(ERROR) << "failed to listen on " << bindAddr << ":" << port;
        return 1;
    }
    return 0;
}