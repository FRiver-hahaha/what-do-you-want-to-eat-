#pragma once

#include <string>

#include "server/business/profile/ProfileDao.h"
#include "server/database/MySQLPool.h"
#include "server/identity/IdentityService.h"
#include "shared/protocol/JsonValue.h"
#include "shared/protocol/protocol.h"

namespace eat::cache {
class CacheClient;
}

namespace eat::business {

struct ProfileResult {
    bool ok = false;
    protocol::ErrorCode error = protocol::ErrorCode::Ok;
    std::string message;
    protocol::JsonValue data;
};

class ProfileService {
 public:
    ProfileService(db::MySQLPool& pool, identity::IdentityService& identity,
                   cache::CacheClient* cache = nullptr)
        : pool_(pool), identity_(identity), cache_(cache) {}

    ProfileResult Get(const std::string& user_key);
    ProfileResult Patch(const std::string& user_key, const protocol::JsonValue& body);

 private:
    db::MySQLPool& pool_;
    identity::IdentityService& identity_;
    cache::CacheClient* cache_ = nullptr;
};

}  // namespace eat::business
