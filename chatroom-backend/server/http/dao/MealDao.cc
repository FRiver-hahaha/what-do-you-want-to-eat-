#include "http/dao/MealDao.h"

#include <mysql/mysql.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "database/MySQLPool.h"
#include "http/DbConn.h"

namespace {

std::string quote(MYSQL* conn, const std::string& value) {
    std::string escaped(value.size() * 2 + 1, '\0');
    const unsigned long n = mysql_real_escape_string(
        conn, &escaped[0], value.c_str(), value.size());
    escaped.resize(n);
    return "'" + escaped + "'";
}

std::string number(double value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", value);
    return std::string(buf);
}

std::string cell(MYSQL_ROW row, unsigned long* lengths, int index) {
    if (row[index] == nullptr) {
        return std::string();
    }
    return std::string(row[index], static_cast<std::size_t>(lengths[index]));
}

const char* kColumns =
    "`id`,`date`,`type`,`time`,`text`,`items`,`protein`,`fiber`,`veggies`,"
    "`water`,`penalty`";

MealRow readRow(MYSQL_ROW row, unsigned long* lengths) {
    MealRow out;
    out.id = std::strtoull(cell(row, lengths, 0).c_str(), nullptr, 10);
    out.date = cell(row, lengths, 1);
    out.type = cell(row, lengths, 2);
    out.time = cell(row, lengths, 3);
    out.text = cell(row, lengths, 4);
    out.items = cell(row, lengths, 5);
    out.protein = std::atof(cell(row, lengths, 6).c_str());
    out.fiber = std::atof(cell(row, lengths, 7).c_str());
    out.veggies = std::atof(cell(row, lengths, 8).c_str());
    out.water = std::atof(cell(row, lengths, 9).c_str());
    out.penalty = std::atoi(cell(row, lengths, 10).c_str());
    return out;
}

}  // namespace

unsigned long long MealDao::insertOn(MYSQL* conn, const std::string& agentUid,
                                     const MealRow& row) {
    const std::string sql =
        "INSERT INTO `meal_records` "
        "(`agent_uid`,`date`,`type`,`time`,`text`,`items`,`protein`,`fiber`,"
        "`veggies`,`water`,`penalty`) VALUES (" +
        quote(conn, agentUid) + "," + quote(conn, row.date) + "," +
        quote(conn, row.type) + "," + quote(conn, row.time) + "," +
        quote(conn, row.text) + "," + quote(conn, row.items) + "," +
        number(row.protein) + "," + number(row.fiber) + "," +
        number(row.veggies) + "," + number(row.water) + "," +
        std::to_string(row.penalty) + ")";
    if (mysql_query(conn, sql.c_str()) != 0) {
        return 0;
    }
    const unsigned long long id =
        static_cast<unsigned long long>(mysql_insert_id(conn));
    return id;
}

MealDao::Lookup MealDao::loadByIdOn(MYSQL* conn, const std::string& agentUid,
                                    unsigned long long id, MealRow& out) {
    const std::string sql =
        "SELECT " + std::string(kColumns) +
        " FROM `meal_records` WHERE `agent_uid`=" + quote(conn, agentUid) +
        " AND `id`=" + std::to_string(id) + " LIMIT 1";
    if (mysql_query(conn, sql.c_str()) != 0) {
        return Lookup::Unavailable;
    }
    MYSQL_RES* result = mysql_store_result(conn);
    if (result == nullptr) {
        return Lookup::Unavailable;
    }
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row == nullptr) {
        mysql_free_result(result);
        return Lookup::NotFound;
    }
    unsigned long* lengths = mysql_fetch_lengths(result);
    out = readRow(row, lengths);
    mysql_free_result(result);
    return Lookup::Found;
}

MealDao::Lookup MealDao::listRangeOn(MYSQL* conn, const std::string& agentUid,
                                     const std::string& fromDate,
                                     const std::string& toDate,
                                     std::vector<MealRow>& out) {
    out.clear();
    const std::string sql =
        "SELECT " + std::string(kColumns) +
        " FROM `meal_records` WHERE `agent_uid`=" + quote(conn, agentUid) +
        " AND `date`>=" + quote(conn, fromDate) +
        " AND `date`<=" + quote(conn, toDate) +
        " ORDER BY `date` ASC, `id` ASC";
    if (mysql_query(conn, sql.c_str()) != 0) {
        return Lookup::Unavailable;
    }
    MYSQL_RES* result = mysql_store_result(conn);
    if (result == nullptr) {
        return Lookup::Unavailable;
    }
    unsigned long* lengths = nullptr;
    MYSQL_ROW row = nullptr;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        lengths = mysql_fetch_lengths(result);
        out.push_back(readRow(row, lengths));
    }
    mysql_free_result(result);
    return Lookup::Found;
}

MealDao::Lookup MealDao::listRange(const std::string& agentUid,
                                   const std::string& fromDate,
                                   const std::string& toDate,
                                   std::vector<MealRow>& out) const {
    DbConn conn(m_pool);
    if (!conn) {
        return Lookup::Unavailable;
    }
    return listRangeOn(conn.get(), agentUid, fromDate, toDate, out);
}