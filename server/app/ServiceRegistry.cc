#include "server/app/ServiceRegistry.h"

#include <utility>

#include "server/app/Log.h"

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
    profile_ = std::make_unique<business::ProfileService>(*pool_, *identity_);
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
    Log::Write(LogLevel::Info, "service", "handlers_registered");
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