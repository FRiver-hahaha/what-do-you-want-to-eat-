#pragma once

#include <string>

#include "server/business/common/BusinessResult.h"
#include "server/database/MySQLPool.h"
#include "shared/protocol/JsonValue.h"

namespace eat::cache {
class CacheClient;
}

namespace eat::business {

class StatsService {
 public:
    explicit StatsService(db::MySQLPool& pool, cache::CacheClient* cache = nullptr)
        : pool_(pool), cache_(cache) {}

    BusinessResult NutritionGap(const std::string& user_key,
                                const protocol::JsonValue& params);
    BusinessResult Weekly(const std::string& user_key,
                          const protocol::JsonValue& params);

 private:
    db::MySQLPool& pool_;
    cache::CacheClient* cache_ = nullptr;
};

}  // namespace eat::business
