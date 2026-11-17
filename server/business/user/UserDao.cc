#include "server/business/user/UserDao.h"

#include <mysql/mysql.h>

#include <cstdlib>

#include "server/database/DbConn.h"

namespace eat::business {
namespace {

std::string Cell(MYSQL_ROW row, unsigned long* lengths, int index) {
    if (row == nullptr || row[index] == nullptr) return std::string();
    return std::string(row[index], static_cast<std::size_t>(lengths[index]));
}

}  // namespace

UserDao::Lookup UserDao::FindByUserKey(const std::string& user_key) const {
    db::DbConn conn(pool_);
    if (!conn) return Lookup::Unavailable;
    const std::string sql = "SELECT 1 FROM `account` WHERE `user_key`='" +
                            conn.escape(user_key) + "' LIMIT 1";
    if (mysql_query(conn.get(), sql.c_str()) != 0) return Lookup::Unavailable;
    MYSQL_RES* result = mysql_store_result(conn.get());
    if (result == nullptr) return Lookup::Unavailable;
    MYSQL_ROW row = mysql_fetch_row(result);
    mysql_free_result(result);
    return row != nullptr ? Lookup::Found : Lookup::NotFound;
}

UserDao::Lookup UserDao::LoadByUserKey(const std::string& user_key,
                                        AccountIdentity& out) const {
    db::DbConn conn(pool_);
    if (!conn) return Lookup::Unavailable;
    const std::string sql =
        "SELECT `account_id`,`user_key`,`display_name` FROM `account` "
        "WHERE `user_key`='" + conn.escape(user_key) + "' LIMIT 1";
    if (mysql_query(conn.get(), sql.c_str()) != 0) return Lookup::Unavailable;
    MYSQL_RES* result = mysql_store_result(conn.get());
    if (result == nullptr) return Lookup::Unavailable;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row == nullptr) {
        mysql_free_result(result);
        return Lookup::NotFound;
    }
    unsigned long* lengths = mysql_fetch_lengths(result);
    out.account_id = std::strtoull(Cell(row, lengths, 0).c_str(), nullptr, 10);
    out.user_key = Cell(row, lengths, 1);
    out.display_name = Cell(row, lengths, 2);
    mysql_free_result(result);
    return Lookup::Found;
}

}  // namespace eat::business
