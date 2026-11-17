#pragma once

#include <string>
#include <vector>

#include "server/database/MySQLPool.h"

namespace eat::business {

struct FriendMember {
    std::string user_key;
    std::string name;
};

struct FriendRelation {
    bool found = false;
    int status = 0;
    std::string blocked_by;
};

class FriendDao {
 public:
    enum class Lookup { Found, NotFound, Unavailable };

    explicit FriendDao(db::MySQLPool& pool) : pool_(pool) {}

    Lookup ListFriends(const std::string& user_key, std::vector<FriendMember>& out) const;
    Lookup LoadRelation(const std::string& owner_key, const std::string& peer_key,
                        FriendRelation& out) const;
    bool UpsertRelation(const std::string& owner_key, const std::string& peer_key,
                        int status, const std::string& blocked_by);
    bool DeleteRelation(const std::string& owner_key, const std::string& peer_key);
    bool DeleteBoth(const std::string& left_key, const std::string& right_key);

 private:
    db::MySQLPool& pool_;
};

}  // namespace eat::business
