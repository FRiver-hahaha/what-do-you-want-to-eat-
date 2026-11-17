#pragma once

#include <string>

#include "server/business/common/BusinessResult.h"
#include "server/database/MySQLPool.h"
#include "shared/protocol/JsonValue.h"

namespace eat::business {

class RankingService {
 public:
    explicit RankingService(db::MySQLPool& pool) : pool_(pool) {}

    BusinessResult Friends(const std::string& user_key, const protocol::JsonValue& params);

 private:
    bool ScoreFor(const std::string& user_key, const std::string& scope, double& out) const;

    db::MySQLPool& pool_;
};

}  // namespace eat::business
