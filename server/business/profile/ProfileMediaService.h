#pragma once

#include <string>

#include "server/business/common/BusinessResult.h"
#include "server/database/MySQLPool.h"
#include "server/identity/IdentityService.h"
#include "shared/protocol/JsonValue.h"

namespace eat::business {

// Ranking artwork service. Writes are always bound to the connection's
// user_key; reads may target a peer only when a confirmed friendship exists.
class ProfileMediaService {
 public:
    ProfileMediaService(db::MySQLPool& pool, identity::IdentityService& identity)
        : pool_(pool), identity_(identity) {}

    BusinessResult Get(const std::string& requester_key, const protocol::JsonValue& params);
    BusinessResult Set(const std::string& user_key, const protocol::JsonValue& params);

 private:
    db::MySQLPool& pool_;
    identity::IdentityService& identity_;
};

}  // namespace eat::business
