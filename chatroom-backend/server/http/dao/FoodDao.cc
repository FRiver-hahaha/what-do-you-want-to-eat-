#include "http/dao/FoodDao.h"

#include <mysql/mysql.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "database/MySQLPool.h"
#include "http/DbConn.h"

namespace {

std::string cell(MYSQL_ROW row, unsigned long* lengths, int index) {
    if (row[index] == nullptr) {
        return std::string();
    }
    return std::string(row[index], static_cast<std::size_t>(lengths[index]));
}

FoodRow readRow(MYSQL_ROW row, unsigned long* lengths) {
    FoodRow out;
    out.id = cell(row, lengths, 0);
    out.name = cell(row, lengths, 1);
    out.category = cell(row, lengths, 2);
    out.description = cell(row, lengths, 3);
    out.calories = std::atof(cell(row, lengths, 4).c_str());
    out.protein = std::atof(cell(row, lengths, 5).c_str());
    out.fiber = std::atof(cell(row, lengths, 6).c_str());
    out.veggies = std::atof(cell(row, lengths, 7).c_str());
    out.water = std::atof(cell(row, lengths, 8).c_str());
    out.nature = cell(row, lengths, 9);
    out.keywords = cell(row, lengths, 10);
    out.image = cell(row, lengths, 11);
    out.mealTypes = cell(row, lengths, 12);
    out.price = std::atoi(cell(row, lengths, 13).c_str());
    out.available = std::atoi(cell(row, lengths, 14).c_str()) != 0;
    return out;
}

}  // namespace

FoodDao::Lookup FoodDao::listAvailable(std::vector<FoodRow>& out) const {
    out.clear();
    DbConn conn(m_pool);
    if (!conn) {
        return Lookup::Unavailable;
    }

    // ORDER BY id 只保证响应稳定；Agent 不依赖菜品顺序。
    const std::string sql =
        "SELECT `id`,`name`,`category`,`description`,`calories`,`protein`,"
        "`fiber`,`veggies`,`water`,`nature`,`keywords`,`image`,`meal_types`,"
        "`price`,`available` FROM `foods` WHERE `available`=1 ORDER BY `id` ASC";
    if (mysql_query(conn.get(), sql.c_str()) != 0) {
        return Lookup::Unavailable;
    }
    MYSQL_RES* result = mysql_store_result(conn.get());
    if (result == nullptr) {
        return Lookup::Unavailable;
    }
    MYSQL_ROW row = nullptr;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        unsigned long* lengths = mysql_fetch_lengths(result);
        out.push_back(readRow(row, lengths));
    }
    mysql_free_result(result);
    return Lookup::Found;
}