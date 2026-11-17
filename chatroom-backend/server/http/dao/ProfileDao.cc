#include "http/dao/ProfileDao.h"

#include <mysql/mysql.h>

#include <cstdlib>

#include "database/MySQLPool.h"
#include "http/DbConn.h"

namespace {

std::string cell(MYSQL_ROW row, unsigned long* lengths, int index) {
    if (row[index] == nullptr) {
        return std::string();
    }
    return std::string(row[index], static_cast<std::size_t>(lengths[index]));
}

}  // namespace

ProfileDao::Lookup ProfileDao::load(const std::string& agentUid,
                                   HealthProfileRow& out) const {
    DbConn conn(m_pool);
    if (!conn) {
        return Lookup::Unavailable;
    }

    const std::string sql =
        "SELECT `name`,`gender`,`age`,`height`,`weight`,`goal`,`activity`,"
        "`diet_style`,`water_target`,`sleep_hours`,`allergies`,`dislikes`,"
        "`spend_min`,`spend_max`,`note` FROM `health_profile` "
        "WHERE `agent_uid`='" +
        conn.escape(agentUid) + "' LIMIT 1";
    if (mysql_query(conn.get(), sql.c_str()) != 0) {
        return Lookup::Unavailable;
    }
    MYSQL_RES* result = mysql_store_result(conn.get());
    if (result == nullptr) {
        return Lookup::Unavailable;
    }
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row == nullptr) {
        mysql_free_result(result);
        return Lookup::NotFound;
    }
    unsigned long* lengths = mysql_fetch_lengths(result);

    out.name = cell(row, lengths, 0);
    out.gender = cell(row, lengths, 1);
    out.age = std::atoi(cell(row, lengths, 2).c_str());
    out.height = std::atof(cell(row, lengths, 3).c_str());
    out.weight = std::atof(cell(row, lengths, 4).c_str());
    out.goal = cell(row, lengths, 5);
    out.activity = cell(row, lengths, 6);
    out.dietStyle = cell(row, lengths, 7);
    out.waterTarget = std::atoi(cell(row, lengths, 8).c_str());
    out.sleepHours = std::atof(cell(row, lengths, 9).c_str());
    out.allergies = cell(row, lengths, 10);
    out.dislikes = cell(row, lengths, 11);
    out.spendMin = std::atoi(cell(row, lengths, 12).c_str());
    out.spendMax = std::atoi(cell(row, lengths, 13).c_str());
    out.note = cell(row, lengths, 14);

    mysql_free_result(result);
    return Lookup::Found;
}

bool ProfileDao::save(const std::string& agentUid,
                      const HealthProfileRow& row) const {
    DbConn conn(m_pool);
    if (!conn) {
        return false;
    }

    auto text = [&conn](const std::string& value) {
        return "'" + conn.escape(value) + "'";
    };

    const std::string sql =
        "INSERT INTO `health_profile` "
        "(`agent_uid`,`name`,`gender`,`age`,`height`,`weight`,`goal`,`activity`,"
        "`diet_style`,`water_target`,`sleep_hours`,`allergies`,`dislikes`,"
        "`spend_min`,`spend_max`,`note`) VALUES (" +
        text(agentUid) + "," + text(row.name) + "," + text(row.gender) + "," +
        std::to_string(row.age) + "," + std::to_string(row.height) + "," +
        std::to_string(row.weight) + "," + text(row.goal) + "," +
        text(row.activity) + "," + text(row.dietStyle) + "," +
        std::to_string(row.waterTarget) + "," + std::to_string(row.sleepHours) +
        "," + text(row.allergies) + "," + text(row.dislikes) + "," +
        std::to_string(row.spendMin) + "," + std::to_string(row.spendMax) + "," +
        text(row.note) +
        ") AS new ON DUPLICATE KEY UPDATE "
        "`name`=new.`name`,`gender`=new.`gender`,`age`=new.`age`,"
        "`height`=new.`height`,`weight`=new.`weight`,`goal`=new.`goal`,"
        "`activity`=new.`activity`,`diet_style`=new.`diet_style`,"
        "`water_target`=new.`water_target`,`sleep_hours`=new.`sleep_hours`,"
        "`allergies`=new.`allergies`,`dislikes`=new.`dislikes`,"
        "`spend_min`=new.`spend_min`,`spend_max`=new.`spend_max`,`note`=new.`note`";

    return mysql_query(conn.get(), sql.c_str()) == 0;
}