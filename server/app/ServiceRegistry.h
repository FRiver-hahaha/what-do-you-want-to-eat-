#pragma once

#include <functional>
#include <memory>
#include <string>

#include "server/app/Config.h"
#include "server/business/common/BusinessResult.h"
#include "server/business/food/FoodService.h"
#include "server/business/friend/FriendService.h"
#include "server/business/meal/MealService.h"
#include "server/business/profile/ProfileService.h"
#include "server/business/ranking/RankingService.h"
#include "server/business/stats/StatsService.h"
#include "server/cache/CacheClient.h"
#include "server/cache/CacheKeyBuilder.h"
#include "server/database/MySQLPool.h"
#include "server/database/RedisClient.h"
#include "server/identity/CaptchaStore.h"
#include "server/identity/IdentityService.h"
#include "server/identity/TokenVerifier.h"
#include "server/tcp/FrameDispatcher.h"

namespace eat::app {

class ServiceRegistry {
 public:
    ServiceRegistry(const Config& config, tcp::FrameDispatcher& dispatcher);
    ~ServiceRegistry();

    bool Init(std::string& error);
    void RegisterHandlers();

    db::MySQLPool& pool() { return *pool_; }

 private:
    using ServiceHandler =
        std::function<business::BusinessResult(const std::string&, const protocol::JsonValue&)>;

    bool HandleHandshake(tcp::ConnContext& context, const protocol::JsonValue& body,
                         protocol::JsonValue& response, protocol::ErrorCode& error,
                         std::string& message);
    bool HandleProfileGet(tcp::ConnContext& context, const protocol::JsonValue& params,
                          protocol::JsonValue& data, protocol::ErrorCode& error,
                          std::string& message);
    bool HandleProfilePatch(tcp::ConnContext& context, const protocol::JsonValue& params,
                            protocol::JsonValue& data, protocol::ErrorCode& error,
                            std::string& message);
    bool HandleAuthLogin(tcp::ConnContext& context, const protocol::JsonValue& params,
                         protocol::JsonValue& data, protocol::ErrorCode& error,
                         std::string& message);
    bool HandleAuthRegister(tcp::ConnContext& context, const protocol::JsonValue& params,
                            protocol::JsonValue& data, protocol::ErrorCode& error,
                            std::string& message);

    void RegisterBusiness(const char* method, ServiceHandler handler);
    static protocol::ErrorCode MapStatus(identity::IdentityStatus status);

    const Config& config_;
    tcp::FrameDispatcher& dispatcher_;
    std::unique_ptr<db::MySQLPool> pool_;
    std::unique_ptr<db::RedisClient> redis_;
    std::unique_ptr<cache::CacheKeyBuilder> cache_keys_;
    std::unique_ptr<cache::CacheClient> cache_;
    std::unique_ptr<identity::IdentityService> identity_;
    std::unique_ptr<identity::TokenVerifier> verifier_;
    std::unique_ptr<identity::CaptchaStore> captcha_;
    std::unique_ptr<business::ProfileService> profile_;
    std::unique_ptr<business::MealService> meal_;
    std::unique_ptr<business::StatsService> stats_;
    std::unique_ptr<business::FoodService> food_;
    std::unique_ptr<business::FriendService> friend_;
    std::unique_ptr<business::RankingService> ranking_;
};

}  // namespace eat::app
