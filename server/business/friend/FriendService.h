#pragma once

#include <string>

#include "server/business/common/BusinessResult.h"
#include "server/database/MySQLPool.h"
#include "shared/protocol/JsonValue.h"

namespace eat::cache {
class CacheClient;
}

namespace eat::business {

class FriendService {
 public:
    explicit FriendService(db::MySQLPool& pool, cache::CacheClient* cache = nullptr)
        : pool_(pool), cache_(cache) {}

    BusinessResult List(const std::string& user_key, const protocol::JsonValue& params);
    BusinessResult Request(const std::string& user_key, const protocol::JsonValue& params);
    BusinessResult Accept(const std::string& user_key, const protocol::JsonValue& params);
    BusinessResult Remove(const std::string& user_key, const protocol::JsonValue& params);
    BusinessResult Block(const std::string& user_key, const protocol::JsonValue& params);

 private:
    db::MySQLPool& pool_;
    cache::CacheClient* cache_ = nullptr;
};

}  // namespace eat::business
