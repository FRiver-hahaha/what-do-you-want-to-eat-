#include "server/business/meal/MealDao.h"

#include <cstdio>
#include <cstdlib>

#include "server/database/DbConn.h"

namespace eat::business {
namespace {

std::string Quote(MYSQL* conn, const std::string& value) {
    std::string escaped(value.size() * 2 + 1, '\0');
    const unsigned long n =
        mysql_real_escape_string(conn, &escaped[0], value.c_str(), value.size());
    escaped.resize(n);
    return "'" + escaped + "'";
}

std::string Number(double value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", value);
    return std::string(buf);
}

std::string Cell(MYSQL_ROW row, unsigned long* lengths, int index) {
    if (row == nullptr || row[index] == nullptr) return std::string();
    return std::string(row[index], static_cast<std::size_t>(lengths[index]));
}

const char* kColumns =
    "`id`,`date`,`type`,`time`,`text`,`items`,`protein`,`fiber`,`veggies`,"
    "`water`,`penalty`,`version`";

MealRow ReadRow(MYSQL_ROW row, unsigned long* lengths) {
    MealRow out;
    out.id = std::strtoull(Cell(row, lengths, 0).c_str(), nullptr, 10);
    out.date = Cell(row, lengths, 1);
    out.type = Cell(row, lengths, 2);
    out.time = Cell(row, lengths, 3);
    out.text = Cell(row, lengths, 4);
    out.items = Cell(row, lengths, 5);
    out.protein = std::atof(Cell(row, lengths, 6).c_str());
    out.fiber = std::atof(Cell(row, lengths, 7).c_str());
    out.veggies = std::atof(Cell(row, lengths, 8).c_str());
    out.water = std::atof(Cell(row, lengths, 9).c_str());
    out.penalty = std::atoi(Cell(row, lengths, 10).c_str());
    out.version = static_cast<unsigned int>(std::strtoul(Cell(row, lengths, 11).c_str(),
                                                         nullptr, 10));
    return out;
}

}  // namespace

unsigned long long MealDao::InsertOn(MYSQL* conn, const std::string& user_key,
                                     const MealRow& row) {
    const std::string sql =
        "INSERT INTO `meal_records` "
        "(`agent_uid`,`user_key`,`date`,`type`,`time`,`text`,`items`,`protein`,`fiber`,"
        "`veggies`,`water`,`penalty`,`version`) VALUES (" +
        Quote(conn, user_key) + "," + Quote(conn, user_key) + "," + Quote(conn, row.date) +
        "," + Quote(conn, row.type) + "," + Quote(conn, row.time) + "," +
        Quote(conn, row.text) + "," + Quote(conn, row.items) + "," +
        Number(row.protein) + "," + Number(row.fiber) + "," + Number(row.veggies) + "," +
        Number(row.water) + "," + std::to_string(row.penalty) + ",1)";
    if (mysql_query(conn, sql.c_str()) != 0) return 0;
    return static_cast<unsigned long long>(mysql_insert_id(conn));
}

MealDao::Lookup MealDao::LoadByIdOn(MYSQL* conn, const std::string& user_key,
                                    unsigned long long id, MealRow& out) {
    const std::string sql = "SELECT " + std::string(kColumns) +
                            " FROM `meal_records` WHERE `user_key`=" + Quote(conn, user_key) +
                            " AND `id`=" + std::to_string(id) + " LIMIT 1";
    if (mysql_query(conn, sql.c_str()) != 0) return Lookup::Unavailable;
    MYSQL_RES* result = mysql_store_result(conn);
    if (result == nullptr) return Lookup::Unavailable;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row == nullptr) {
        mysql_free_result(result);
        return Lookup::NotFound;
    }
    unsigned long* lengths = mysql_fetch_lengths(result);
    out = ReadRow(row, lengths);
    mysql_free_result(result);
    return Lookup::Found;
}

MealDao::Lookup MealDao::LoadByIdForUpdateOn(MYSQL* conn, const std::string& user_key,
                                             unsigned long long id, MealRow& out) {
    const std::string sql = "SELECT " + std::string(kColumns) +
                            " FROM `meal_records` WHERE `user_key`=" + Quote(conn, user_key) +
                            " AND `id`=" + std::to_string(id) +
                            " AND `deleted_at` IS NULL LIMIT 1 FOR UPDATE";
    if (mysql_query(conn, sql.c_str()) != 0) return Lookup::Unavailable;
    MYSQL_RES* result = mysql_store_result(conn);
    if (result == nullptr) return Lookup::Unavailable;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row == nullptr) {
        mysql_free_result(result);
        return Lookup::NotFound;
    }
    unsigned long* lengths = mysql_fetch_lengths(result);
    out = ReadRow(row, lengths);
    mysql_free_result(result);
    return Lookup::Found;
}

MealDao::Lookup MealDao::ListRangeOn(MYSQL* conn, const std::string& user_key,
                                     const std::string& from_date,
                                     const std::string& to_date,
                                     std::vector<MealRow>& out) {
    out.clear();
    const std::string sql =
        "SELECT " + std::string(kColumns) +
        " FROM `meal_records` WHERE `user_key`=" + Quote(conn, user_key) +
        " AND `date`>=" + Quote(conn, from_date) + " AND `date`<=" +
        Quote(conn, to_date) + " AND `deleted_at` IS NULL ORDER BY `date` ASC, `id` ASC";
    if (mysql_query(conn, sql.c_str()) != 0) return Lookup::Unavailable;
    MYSQL_RES* result = mysql_store_result(conn);
    if (result == nullptr) return Lookup::Unavailable;
    MYSQL_ROW row = nullptr;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        unsigned long* lengths = mysql_fetch_lengths(result);
        out.push_back(ReadRow(row, lengths));
    }
    mysql_free_result(result);
    return Lookup::Found;
}

MealDao::UpdateResult MealDao::UpdateOn(MYSQL* conn, const std::string& user_key,
                                        unsigned long long id, unsigned int expected_version,
                                        const MealRow& row) {
    const std::string sql =
        "UPDATE `meal_records` SET `date`=" + Quote(conn, row.date) +
        ",`type`=" + Quote(conn, row.type) + ",`time`=" + Quote(conn, row.time) +
        ",`text`=" + Quote(conn, row.text) + ",`items`=" + Quote(conn, row.items) +
        ",`protein`=" + Number(row.protein) + ",`fiber`=" + Number(row.fiber) +
        ",`veggies`=" + Number(row.veggies) + ",`water`=" + Number(row.water) +
        ",`penalty`=" + std::to_string(row.penalty) +
        ",`version`=`version`+1 WHERE `user_key`=" + Quote(conn, user_key) +
        " AND `id`=" + std::to_string(id) + " AND `deleted_at` IS NULL AND `version`=" +
        std::to_string(expected_version);
    if (mysql_query(conn, sql.c_str()) != 0) return UpdateResult::Unavailable;
    if (mysql_affected_rows(conn) == 0) {
        MealRow current;
        const Lookup lookup = LoadByIdOn(conn, user_key, id, current);
        if (lookup == Lookup::Unavailable) return UpdateResult::Unavailable;
        if (lookup == Lookup::NotFound) return UpdateResult::NotFound;
        return UpdateResult::VersionConflict;
    }
    return UpdateResult::Ok;
}

MealDao::UpdateResult MealDao::SoftDeleteOn(MYSQL* conn, const std::string& user_key,
                                            unsigned long long id,
                                            unsigned int expected_version) {
    const std::string sql = "UPDATE `meal_records` SET `deleted_at`=NOW(),`version`=`version`+1 "
                            "WHERE `user_key`=" +
                            Quote(conn, user_key) + " AND `id`=" + std::to_string(id) +
                            " AND `deleted_at` IS NULL AND `version`=" +
                            std::to_string(expected_version);
    if (mysql_query(conn, sql.c_str()) != 0) return UpdateResult::Unavailable;
    if (mysql_affected_rows(conn) == 0) {
        MealRow current;
        const Lookup lookup = LoadByIdOn(conn, user_key, id, current);
        if (lookup == Lookup::Unavailable) return UpdateResult::Unavailable;
        if (lookup == Lookup::NotFound) return UpdateResult::NotFound;
        return UpdateResult::VersionConflict;
    }
    return UpdateResult::Ok;
}

MealDao::Lookup MealDao::ListRange(const std::string& user_key,
                                   const std::string& from_date,
                                   const std::string& to_date,
                                   std::vector<MealRow>& out) const {
    db::DbConn conn(pool_);
    if (!conn) return Lookup::Unavailable;
    return ListRangeOn(conn.get(), user_key, from_date, to_date, out);
}

}  // namespace eat::business
