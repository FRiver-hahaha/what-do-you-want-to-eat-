#include "server/business/food/FoodDao.h"

#include <mysql/mysql.h>

#include <cstdlib>

#include "server/database/DbConn.h"

namespace eat::business {
namespace {

std::string Cell(MYSQL_ROW row, unsigned long* lengths, int index) {
    if (row == nullptr || row[index] == nullptr) return std::string();
    return std::string(row[index], static_cast<std::size_t>(lengths[index]));
}

FoodRow ReadRow(MYSQL_ROW row, unsigned long* lengths) {
    FoodRow out;
    out.id = Cell(row, lengths, 0);
    out.name = Cell(row, lengths, 1);
    out.category = Cell(row, lengths, 2);
    out.description = Cell(row, lengths, 3);
    out.calories = std::atof(Cell(row, lengths, 4).c_str());
    out.protein = std::atof(Cell(row, lengths, 5).c_str());
    out.fiber = std::atof(Cell(row, lengths, 6).c_str());
    out.veggies = std::atof(Cell(row, lengths, 7).c_str());
    out.water = std::atof(Cell(row, lengths, 8).c_str());
    out.nature = Cell(row, lengths, 9);
    out.keywords = Cell(row, lengths, 10);
    out.image = Cell(row, lengths, 11);
    out.meal_types = Cell(row, lengths, 12);
    out.price = std::atoi(Cell(row, lengths, 13).c_str());
    out.available = std::atoi(Cell(row, lengths, 14).c_str()) != 0;
    return out;
}

}  // namespace

FoodDao::Lookup FoodDao::ListAvailable(std::vector<FoodRow>& out) const {
    out.clear();
    db::DbConn conn(pool_);
    if (!conn) return Lookup::Unavailable;
    const std::string sql =
        "SELECT `id`,`name`,`category`,`description`,`calories`,`protein`,"
        "`fiber`,`veggies`,`water`,`nature`,`keywords`,`image`,`meal_types`,"
        "`price`,`available` FROM `foods` WHERE `available`=1 ORDER BY `id` ASC";
    if (mysql_query(conn.get(), sql.c_str()) != 0) return Lookup::Unavailable;
    MYSQL_RES* result = mysql_store_result(conn.get());
    if (result == nullptr) return Lookup::Unavailable;
    MYSQL_ROW row = nullptr;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        unsigned long* lengths = mysql_fetch_lengths(result);
        out.push_back(ReadRow(row, lengths));
    }
    mysql_free_result(result);
    return Lookup::Found;
}

}  // namespace eat::business
