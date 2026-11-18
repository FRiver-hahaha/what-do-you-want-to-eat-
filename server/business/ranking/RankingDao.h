#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "server/database/MySQLPool.h"

namespace eat::business {

struct RankingMember {
    std::string user_key;
    std::string name;
    std::uint64_t cover_ver = 0;
    std::uint64_t avatar_ver = 0;
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
