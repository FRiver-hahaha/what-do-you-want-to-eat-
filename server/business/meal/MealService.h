#pragma once

#include <string>

#include "server/business/meal/MealDao.h"
#include "server/database/MySQLPool.h"
#include "server/business/common/BusinessResult.h"
#include "shared/protocol/JsonValue.h"

namespace eat::cache {
class CacheClient;
}

namespace eat::business {

class MealService {
 public:
    explicit MealService(db::MySQLPool& pool, cache::CacheClient* cache = nullptr)
        : pool_(pool), cache_(cache) {}

    BusinessResult Create(const std::string& user_key, const protocol::JsonValue& params);
    BusinessResult Recent(const std::string& user_key, const protocol::JsonValue& params);
    BusinessResult Patch(const std::string& user_key, const protocol::JsonValue& params);
    BusinessResult Delete(const std::string& user_key, const protocol::JsonValue& params);

 private:
    db::MySQLPool& pool_;
    cache::CacheClient* cache_ = nullptr;
};

}  // namespace eat::business
