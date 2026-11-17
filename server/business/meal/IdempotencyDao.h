#pragma once

#include <mysql/mysql.h>

#include <string>

namespace eat::business {

struct IdempotencyRecord {
    std::string payload_hash;
    unsigned long long meal_id = 0;
};

class IdempotencyDao {
 public:
    enum class Lookup { Found, NotFound, Unavailable };
    enum class Insert { Ok, Duplicate, Unavailable };

    static Lookup FindOn(MYSQL* conn, const std::string& user_key,
                         const std::string& idem_key, IdempotencyRecord& out);

    static Insert InsertOn(MYSQL* conn, const std::string& user_key,
                           const std::string& idem_key, const std::string& payload_hash,
                           unsigned long long meal_id);
};

}  // namespace eat::business
