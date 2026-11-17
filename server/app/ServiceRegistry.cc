#include "server/app/ServiceRegistry.h"

#include <utility>
#include <algorithm>

#include "server/app/Log.h"
#include "shared/protocol/methods.h"

namespace eat::app {
namespace {

const protocol::JsonValue* Field(const protocol::JsonValue& body, const char* key) {
    return body.Find(key);
}

bool ReadStringField(const protocol::JsonValue& body, const char* key, std::string& out) {
    const protocol::JsonValue* value = Field(body, key);
    if (value == nullptr || !value->IsString()) return false;
    out = value->AsString();
    return true;
}

const char* StatusText(identity::IdentityStatus status) {
    switch (status) {
        case identity::IdentityStatus::DuplicateLoginId: return "login_id_exists";
        case identity::IdentityStatus::InvalidCredentials: return "invalid_credentials";
        case identity::IdentityStatus::Locked: return "too_many_auth_failures";
        case identity::IdentityStatus::Disabled: return "account_disabled";
        case identity::IdentityStatus::InvalidParams: return "invalid_params";
        case identity::IdentityStatus::Unavailable: return "service_unavailable";
        case identity::IdentityStatus::Ok: return "ok";
    }
    return "internal_error";
}

}  // namespace

ServiceRegistry::ServiceRegistry(const Config& config, tcp::FrameDispatcher& dispatcher)
    : config_(config), dispatcher_(dispatcher) {}

ServiceRegistry::~ServiceRegistry() {
    cache_.reset();
    cache_keys_.reset();
    if (redis_) redis_->Shutdown();
    redis_.reset();
    if (pool_) pool_->Shutdown();
}

bool ServiceRegistry::Init(std::string& error) {
    pool_ = std::make_unique<db::MySQLPool>();
    if (!pool_->Init(static_cast<int>(config_.db_pool_size), config_.mysql_host,
                     config_.mysql_port, config_.mysql_user, config_.mysql_password,
                     config_.mysql_db, config_.mysql_socket, error)) {
        Log::Write(LogLevel::Error, "service",
                   "mysql_pool_init_failed:" + error + ":db=" + config_.mysql_db);
        return false;
    }
    identity_ = std::make_unique<identity::IdentityService>(*pool_);
    verifier_ = std::make_unique<identity::TokenVerifier>(identity_.get());

    db::RedisClient::Options redis_options;
    redis_options.pool_size = static_cast<int>(
        std::min<std::uint32_t>(config_.business_threads, 8));
    redis_options.connect_timeout_ms =
        static_cast<int>(config_.cache_connect_timeout_ms);
    redis_options.io_timeout_ms = static_cast<int>(config_.cache_get_timeout_ms);
    redis_options.password = config_.redis_password;
    redis_ = std::make_unique<db::RedisClient>();
    std::string redis_error;
    if (redis_->Init(config_.redis_host, config_.redis_port, redis_options, redis_error)) {
        cache_keys_ = std::make_unique<cache::CacheKeyBuilder>("v1", config_.cache_gen);
        cache::CacheOptions cache_options;
        cache_options.breaker_failures = config_.cache_breaker_failures;
        cache_options.breaker_open_ms = config_.cache_breaker_open_ms;
        cache_ = std::make_unique<cache::CacheClient>(*redis_, *cache_keys_, cache_options);
    } else {
        Log::Write(LogLevel::Warn, "cache", "redis_unavailable:" + redis_error);
        redis_->Shutdown();
        redis_.reset();
    }

    cache::CacheClient* cache = cache_.get();
    profile_ = std::make_unique<business::ProfileService>(*pool_, *identity_, cache);
    meal_ = std::make_unique<business::MealService>(*pool_, cache);
    stats_ = std::make_unique<business::StatsService>(*pool_, cache);
    food_ = std::make_unique<business::FoodService>(*pool_, cache);
    friend_ = std::make_unique<business::FriendService>(*pool_, cache);
    ranking_ = std::make_unique<business::RankingService>(*pool_);
    return true;
}

protocol::ErrorCode ServiceRegistry::MapStatus(identity::IdentityStatus status) {
    switch (status) {
        case identity::IdentityStatus::Ok: return protocol::ErrorCode::Ok;
        case identity::IdentityStatus::InvalidParams: return protocol::ErrorCode::InvalidParams;
        case identity::IdentityStatus::DuplicateLoginId: return protocol::ErrorCode::InvalidParams;
        case identity::IdentityStatus::InvalidCredentials: return protocol::ErrorCode::Unauthorized;
        case identity::IdentityStatus::Locked: return protocol::ErrorCode::TooManyAuthFailures;
        case identity::IdentityStatus::Disabled: return protocol::ErrorCode::AccountDisabled;
        case identity::IdentityStatus::Unavailable: return protocol::ErrorCode::ServiceUnavailable;
    }
    return protocol::ErrorCode::InternalError;
}

void ServiceRegistry::RegisterHandlers() {
    dispatcher_.SetHandshakeHandler(
        [this](tcp::ConnContext& context, const protocol::JsonValue& body,
               protocol::JsonValue& response, protocol::ErrorCode& error,
               std::string& message) {
            return HandleHandshake(context, body, response, error, message);
        });
    dispatcher_.Register("profile.get",
                         [this](tcp::ConnContext& context, const protocol::JsonValue& params,
                                protocol::JsonValue& data, protocol::ErrorCode& error,
                                std::string& message) {
                             return HandleProfileGet(context, params, data, error, message);
                         });
    dispatcher_.Register("profile.patch",
                         [this](tcp::ConnContext& context, const protocol::JsonValue& params,
                                protocol::JsonValue& data, protocol::ErrorCode& error,
                                std::string& message) {
                             return HandleProfilePatch(context, params, data, error, message);
                         });
    dispatcher_.Register("auth.login",
                         [this](tcp::ConnContext& context, const protocol::JsonValue& params,
                                protocol::JsonValue& data, protocol::ErrorCode& error,
                                std::string& message) {
                             return HandleAuthLogin(context, params, data, error, message);
                         });
    dispatcher_.Register("auth.register",
                         [this](tcp::ConnContext& context, const protocol::JsonValue& params,
                                protocol::JsonValue& data, protocol::ErrorCode& error,
                                std::string& message) {
                             return HandleAuthRegister(context, params, data, error, message);
                         });
    RegisterBusiness(protocol::method::kMealCreate,
                     [this](const std::string& key, const protocol::JsonValue& params) {
                         return meal_->Create(key, params);
                     });
    RegisterBusiness(protocol::method::kMealRecent,
                     [this](const std::string& key, const protocol::JsonValue& params) {
                         return meal_->Recent(key, params);
                     });
    RegisterBusiness(protocol::method::kMealPatch,
                     [this](const std::string& key, const protocol::JsonValue& params) {
                         return meal_->Patch(key, params);
                     });
    RegisterBusiness(protocol::method::kMealDelete,
                     [this](const std::string& key, const protocol::JsonValue& params) {
                         return meal_->Delete(key, params);
                     });
    RegisterBusiness(protocol::method::kStatsNutritionGap,
                     [this](const std::string& key, const protocol::JsonValue& params) {
                         return stats_->NutritionGap(key, params);
                     });
    RegisterBusiness(protocol::method::kStatsWeekly,
                     [this](const std::string& key, const protocol::JsonValue& params) {
                         return stats_->Weekly(key, params);
                     });
    RegisterBusiness(protocol::method::kFoodMenu,
                     [this](const std::string& key, const protocol::JsonValue& params) {
                         return food_->Menu(key, params);
                     });
    RegisterBusiness(protocol::method::kFoodSearch,
                     [this](const std::string& key, const protocol::JsonValue& params) {
                         return food_->Search(key, params);
                     });
    RegisterBusiness(protocol::method::kFriendList,
                     [this](const std::string& key, const protocol::JsonValue& params) {
                         return friend_->List(key, params);
                     });
    RegisterBusiness(protocol::method::kFriendRequest,
                     [this](const std::string& key, const protocol::JsonValue& params) {
                         return friend_->Request(key, params);
                     });
    RegisterBusiness(protocol::method::kFriendAccept,
                     [this](const std::string& key, const protocol::JsonValue& params) {
                         return friend_->Accept(key, params);
                     });
    RegisterBusiness(protocol::method::kFriendRemove,
                     [this](const std::string& key, const protocol::JsonValue& params) {
                         return friend_->Remove(key, params);
                     });
    RegisterBusiness(protocol::method::kFriendBlock,
                     [this](const std::string& key, const protocol::JsonValue& params) {
                         return friend_->Block(key, params);
                     });
    RegisterBusiness(protocol::method::kRankingFriends,
                     [this](const std::string& key, const protocol::JsonValue& params) {
                         return ranking_->Friends(key, params);
                     });
    Log::Write(LogLevel::Info, "service", "handlers_registered");
}

void ServiceRegistry::RegisterBusiness(const char* method, ServiceHandler handler) {
    dispatcher_.Register(
        method, [handler](tcp::ConnContext& context, const protocol::JsonValue& params,
                          protocol::JsonValue& data, protocol::ErrorCode& error,
                          std::string& message) {
            const business::BusinessResult result = handler(context.user_key, params);
            if (!result.ok) {
                error = result.code;
                message = result.message;
                return false;
            }
            data = result.data;
            return true;
        });
}

bool ServiceRegistry::HandleHandshake(tcp::ConnContext& context, const protocol::JsonValue& body,
                                      protocol::JsonValue& response, protocol::ErrorCode& error,
                                      std::string& message) {
    std::string device_id;
    std::string client_version;
    std::string login_id;
    std::string password;
    std::string display_name;
    std::string action;
    ReadStringField(body, "device_id", device_id);
    ReadStringField(body, "client_version", client_version);
    ReadStringField(body, "login_id", login_id);
    ReadStringField(body, "password", password);
    ReadStringField(body, "display_name", display_name);
    ReadStringField(body, "action", action);
    context.device_id = device_id;

    std::string access_token;
    identity::LoginTokens tokens;
    if (ReadStringField(body, "access_token", access_token) && !access_token.empty()) {
        identity::VerifiedSession verified;
        if (!verifier_->Verify(access_token, verified, error)) {
            message = "invalid_access_token";
            return false;
        }
        context.user_key = verified.user_key;
        context.session_id_hash = identity::TokenVerifier::Digest(access_token);
    } else if (!login_id.empty() && !password.empty()) {
        if (action == "register") {
            const identity::IdentityStatus status =
                identity_->Register(login_id, password, display_name, tokens);
            if (status != identity::IdentityStatus::Ok) {
                error = MapStatus(status);
                message = StatusText(status);
                return false;
            }
        }
        const identity::IdentityStatus status =
            identity_->Login(login_id, password, device_id, client_version, tokens);
        if (status != identity::IdentityStatus::Ok) {
            error = MapStatus(status);
            message = StatusText(status);
            return false;
        }
        context.user_key = tokens.user_key;
        context.session_id_hash = identity::TokenVerifier::Digest(tokens.access_token);
    } else {
        error = protocol::ErrorCode::Unauthorized;
        message = "missing_credentials";
        return false;
    }

    response = protocol::JsonValue::Object();
    response.Set("user_key", protocol::JsonValue::String(context.user_key));
    response.Set("session_id", protocol::JsonValue::String(context.session_id_hash));
    response.Set("heartbeat_interval_s",
                 protocol::JsonValue::Int(config_.heartbeat_interval_s));
    response.Set("idle_timeout_s", protocol::JsonValue::Int(config_.idle_timeout_s));
    response.Set("max_frame_size",
                 protocol::JsonValue::Int(static_cast<std::int64_t>(config_.max_frame_size)));
    protocol::JsonValue version = protocol::JsonValue::Object();
    version.Set("major", protocol::JsonValue::Int(1));
    version.Set("minor", protocol::JsonValue::Int(0));
    response.Set("protocol_version", version);
    protocol::JsonValue capabilities = protocol::JsonValue::Array();
    for (const char* name : {"push", "pull_sync", "meal_edit", "friend_write", "idem_v1"}) {
        capabilities.AsArray().push_back(protocol::JsonValue::String(name));
    }
    response.Set("capabilities", capabilities);
    if (!tokens.access_token.empty()) {
        response.Set("access_token", protocol::JsonValue::String(tokens.access_token));
        response.Set("refresh_token", protocol::JsonValue::String(tokens.refresh_token));
    }
    return true;
}

bool ServiceRegistry::HandleProfileGet(tcp::ConnContext& context,
                                       const protocol::JsonValue&,
                                       protocol::JsonValue& data,
                                       protocol::ErrorCode& error, std::string& message) {
    const business::ProfileResult result = profile_->Get(context.user_key);
    if (!result.ok) {
        error = result.error;
        message = result.message;
        return false;
    }
    data = result.data;
    return true;
}

bool ServiceRegistry::HandleProfilePatch(tcp::ConnContext& context,
                                         const protocol::JsonValue& params,
                                         protocol::JsonValue& data,
                                         protocol::ErrorCode& error, std::string& message) {
    const business::ProfileResult result = profile_->Patch(context.user_key, params);
    if (!result.ok) {
        error = result.error;
        message = result.message;
        return false;
    }
    data = result.data;
    return true;
}

bool ServiceRegistry::HandleAuthLogin(tcp::ConnContext& context,
                                      const protocol::JsonValue& params,
                                      protocol::JsonValue& data,
                                      protocol::ErrorCode& error, std::string& message) {
    std::string login_id;
    std::string password;
    if (!ReadStringField(params, "login_id", login_id) ||
        !ReadStringField(params, "password", password)) {
        error = protocol::ErrorCode::InvalidParams;
        message = "login_id_and_password_required";
        return false;
    }
    identity::LoginTokens tokens;
    const identity::IdentityStatus status =
        identity_->Login(login_id, password, context.device_id, "eat-server", tokens);
    if (status != identity::IdentityStatus::Ok) {
        error = MapStatus(status);
        message = StatusText(status);
        return false;
    }
    context.user_key = tokens.user_key;
    data = protocol::JsonValue::Object();
    data.Set("user_key", protocol::JsonValue::String(tokens.user_key));
    data.Set("access_token", protocol::JsonValue::String(tokens.access_token));
    data.Set("refresh_token", protocol::JsonValue::String(tokens.refresh_token));
    return true;
}

bool ServiceRegistry::HandleAuthRegister(tcp::ConnContext& context,
                                         const protocol::JsonValue& params,
                                         protocol::JsonValue& data,
                                         protocol::ErrorCode& error, std::string& message) {
    std::string login_id;
    std::string password;
    std::string display_name;
    if (!ReadStringField(params, "login_id", login_id) ||
        !ReadStringField(params, "password", password)) {
        error = protocol::ErrorCode::InvalidParams;
        message = "login_id_and_password_required";
        return false;
    }
    ReadStringField(params, "display_name", display_name);
    identity::LoginTokens tokens;
    const identity::IdentityStatus registered =
        identity_->Register(login_id, password, display_name, tokens);
    if (registered != identity::IdentityStatus::Ok) {
        error = MapStatus(registered);
        message = StatusText(registered);
        return false;
    }
    const identity::IdentityStatus login = identity_->Login(
        login_id, password, context.device_id, "eat-server", tokens);
    if (login != identity::IdentityStatus::Ok) {
        error = MapStatus(login);
        message = StatusText(login);
        return false;
    }
    context.user_key = tokens.user_key;
    data = protocol::JsonValue::Object();
    data.Set("user_key", protocol::JsonValue::String(tokens.user_key));
    data.Set("access_token", protocol::JsonValue::String(tokens.access_token));
    data.Set("refresh_token", protocol::JsonValue::String(tokens.refresh_token));
    return true;
}

}  // namespace eat::app
