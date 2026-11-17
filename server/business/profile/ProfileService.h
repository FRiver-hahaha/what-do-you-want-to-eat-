#pragma once

#include <string>

#include "server/business/profile/ProfileDao.h"
#include "server/database/MySQLPool.h"
#include "server/identity/IdentityService.h"
#include "shared/protocol/JsonValue.h"
#include "shared/protocol/protocol.h"

namespace eat::business {

struct ProfileResult {
    bool ok = false;
    protocol::ErrorCode error = protocol::ErrorCode::Ok;
    std::string message;
    protocol::JsonValue data;
};

class ProfileService {
 public:
    ProfileService(db::MySQLPool& pool, identity::IdentityService& identity)
        : pool_(pool), identity_(identity) {}

    ProfileResult Get(const std::string& user_key);
    ProfileResult Patch(const std::string& user_key, const protocol::JsonValue& body);

 private:
    db::MySQLPool& pool_;
    identity::IdentityService& identity_;
};

}  // namespace eat::business