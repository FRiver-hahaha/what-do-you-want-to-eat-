#pragma once

#include <memory>
#include <string>

#include "server/app/Config.h"
#include "server/business/profile/ProfileService.h"
#include "server/database/MySQLPool.h"
#include "server/identity/IdentityService.h"
#include "server/identity/TokenVerifier.h"
#include "server/tcp/FrameDispatcher.h"

namespace eat::app {

// Wires storage + identity + business services and registers every protocol
// method. Keeping registration in one place makes the method table auditable:
// an unregistered method stays 3001 instead of silently doing nothing.
class ServiceRegistry {
 public:
    ServiceRegistry(const Config& config, tcp::FrameDispatcher& dispatcher);
    ~ServiceRegistry();

    bool Init(std::string& error);
    void RegisterHandlers();

    db::MySQLPool& pool() { return *pool_; }

 private:
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

    static protocol::ErrorCode MapStatus(identity::IdentityStatus status);

    const Config& config_;
    tcp::FrameDispatcher& dispatcher_;
    std::unique_ptr<db::MySQLPool> pool_;
    std::unique_ptr<identity::IdentityService> identity_;
    std::unique_ptr<identity::TokenVerifier> verifier_;
    std::unique_ptr<business::ProfileService> profile_;
};

}  // namespace eat::app