#pragma once

#include <cstdint>
#include <string>

#include "server/database/MySQLPool.h"

namespace eat::business {

struct AccountIdentity {
    std::uint64_t account_id = 0;
    std::string user_key;
    std::string display_name;
};

class UserDao {
 public:
    enum class Lookup { Found, NotFound, Unavailable };

    explicit UserDao(db::MySQLPool& pool) : pool_(pool) {}

    Lookup FindByUserKey(const std::string& user_key) const;
    Lookup LoadByUserKey(const std::string& user_key, AccountIdentity& out) const;

 private:
    db::MySQLPool& pool_;
};

}  // namespace eat::business
