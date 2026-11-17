#pragma once

#include <mysql/mysql.h>

#include <string>
#include <vector>

#include "server/database/MySQLPool.h"

namespace eat::business {

struct MealRow {
    unsigned long long id = 0;
    std::string date;
    std::string type = "加餐";
    std::string time = "00:00";
    std::string text;
    std::string items = "[]";
    double protein = 0;
    double fiber = 0;
    double veggies = 0;
    double water = 0;
    int penalty = 0;
    unsigned int version = 1;
};

class MealDao {
 public:
    enum class Lookup { Found, NotFound, Unavailable };
    enum class UpdateResult { Ok, NotFound, VersionConflict, Unavailable };

    explicit MealDao(db::MySQLPool& pool) : pool_(pool) {}

    static unsigned long long InsertOn(MYSQL* conn, const std::string& user_key,
                                       const MealRow& row);

    static Lookup LoadByIdOn(MYSQL* conn, const std::string& user_key,
                             unsigned long long id, MealRow& out);

    static Lookup LoadByIdForUpdateOn(MYSQL* conn, const std::string& user_key,
                                      unsigned long long id, MealRow& out);

    static Lookup ListRangeOn(MYSQL* conn, const std::string& user_key,
                              const std::string& from_date, const std::string& to_date,
                              std::vector<MealRow>& out);

    static UpdateResult UpdateOn(MYSQL* conn, const std::string& user_key,
                                 unsigned long long id, unsigned int expected_version,
                                 const MealRow& row);

    static UpdateResult SoftDeleteOn(MYSQL* conn, const std::string& user_key,
                                     unsigned long long id, unsigned int expected_version);

    Lookup ListRange(const std::string& user_key, const std::string& from_date,
                     const std::string& to_date, std::vector<MealRow>& out) const;

 private:
    db::MySQLPool& pool_;
};

}  // namespace eat::business
