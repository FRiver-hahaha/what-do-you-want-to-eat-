#pragma once

#include <string>
#include <vector>

#include "server/database/MySQLPool.h"

namespace eat::business {

struct RankingMember {
    std::string user_key;
    std::string name;
};

class RankingDao {
 public:
    enum class Lookup { Found, Unavailable };

    explicit RankingDao(db::MySQLPool& pool) : pool_(pool) {}

    Lookup ListFriends(const std::string& user_key, std::vector<RankingMember>& out) const;

 private:
    db::MySQLPool& pool_;
};

}  // namespace eat::business
