#include "server/business/profile/ProfileDao.h"

#include <mysql/mysql.h>

#include <cstdlib>
#include <string>

#include "server/database/DbConn.h"

namespace eat::business {
namespace {

std::string Cell(MYSQL_ROW row, unsigned long* lengths, int index) {
    if (row == nullptr || row[index] == nullptr) return std::string();
    return std::string(row[index], static_cast<std::size_t>(lengths[index]));
}

}  // namespace

ProfileDao::Lookup ProfileDao::Load(const std::string& user_key, HealthProfileRow& out) {
    db::DbConn conn(pool_);
    if (!conn) return Lookup::Unavailable;
    const std::string sql =
        "SELECT `name`,`gender`,`age`,`height`,`weight`,`goal`,`activity`,"
        "`diet_style`,`water_target`,`sleep_hours`,`allergies`,`dislikes`,"
        "`spend_min`,`spend_max`,`note` FROM `health_profile` "
        "WHERE `agent_uid`='" + conn.escape(user_key) + "' LIMIT 1";
    if (mysql_query(conn.get(), sql.c_str()) != 0) return Lookup::Unavailable;
    MYSQL_RES* result = mysql_store_result(conn.get());
    if (result == nullptr) return Lookup::Unavailable;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row == nullptr) {
        mysql_free_result(result);
        return Lookup::NotFound;
    }
    unsigned long* lengths = mysql_fetch_lengths(result);
    out.name = Cell(row, lengths, 0);
    out.gender = Cell(row, lengths, 1);
    out.age = std::atoi(Cell(row, lengths, 2).c_str());
    out.height = std::atof(Cell(row, lengths, 3).c_str());
    out.weight = std::atof(Cell(row, lengths, 4).c_str());
    out.goal = Cell(row, lengths, 5);
    out.activity = Cell(row, lengths, 6);
    out.diet_style = Cell(row, lengths, 7);
    out.water_target = std::atoi(Cell(row, lengths, 8).c_str());
    out.sleep_hours = std::atof(Cell(row, lengths, 9).c_str());
    out.allergies = Cell(row, lengths, 10);
    out.dislikes = Cell(row, lengths, 11);
    out.spend_min = std::atoi(Cell(row, lengths, 12).c_str());
    out.spend_max = std::atoi(Cell(row, lengths, 13).c_str());
    out.note = Cell(row, lengths, 14);
    mysql_free_result(result);
    return Lookup::Found;
}

bool ProfileDao::Save(const std::string& user_key, const HealthProfileRow& row) {
    db::DbConn conn(pool_);
    if (!conn) return false;
    auto text = [&conn](const std::string& value) { return "'" + conn.escape(value) + "'"; };
    const std::string sql =
        "INSERT INTO `health_profile` "
        "(`agent_uid`,`name`,`gender`,`age`,`height`,`weight`,`goal`,`activity`,"
        "`diet_style`,`water_target`,`sleep_hours`,`allergies`,`dislikes`,"
        "`spend_min`,`spend_max`,`note`) VALUES (" +
        text(user_key) + "," + text(row.name) + "," + text(row.gender) + "," +
        std::to_string(row.age) + "," + std::to_string(row.height) + "," +
        std::to_string(row.weight) + "," + text(row.goal) + "," + text(row.activity) +
        "," + text(row.diet_style) + "," + std::to_string(row.water_target) + "," +
        std::to_string(row.sleep_hours) + "," + text(row.allergies) + "," +
        text(row.dislikes) + "," + std::to_string(row.spend_min) + "," +
        std::to_string(row.spend_max) + "," + text(row.note) +
        ") AS new ON DUPLICATE KEY UPDATE "
        "`name`=new.`name`,`gender`=new.`gender`,`age`=new.`age`,"
        "`height`=new.`height`,`weight`=new.`weight`,`goal`=new.`goal`,"
        "`activity`=new.`activity`,`diet_style`=new.`diet_style`,"
        "`water_target`=new.`water_target`,`sleep_hours`=new.`sleep_hours`,"
        "`allergies`=new.`allergies`,`dislikes`=new.`dislikes`,"
        "`spend_min`=new.`spend_min`,`spend_max`=new.`spend_max`,`note`=new.`note`";
    return mysql_query(conn.get(), sql.c_str()) == 0;
}

}  // namespace eat::business
