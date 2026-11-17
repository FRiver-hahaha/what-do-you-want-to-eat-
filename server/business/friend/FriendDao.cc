#include "server/business/friend/FriendDao.h"

#include <mysql/mysql.h>

#include <string>

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

std::string Cell(MYSQL_ROW row, unsigned long* lengths, int index) {
    if (row == nullptr || row[index] == nullptr) return std::string();
    return std::string(row[index], static_cast<std::size_t>(lengths[index]));
}

}  // namespace

FriendDao::Lookup FriendDao::ListFriends(const std::string& user_key,
                                         std::vector<FriendMember>& out) const {
    out.clear();
    db::DbConn conn(pool_);
    if (!conn) return Lookup::Unavailable;

    const std::string sql =
        "SELECT x.`peer_key`, COALESCE(NULLIF(p.`name`,''), NULLIF(a.`display_name`,''), "
        "x.`peer_key`) AS display_name FROM ("
        " SELECT CASE WHEN `owner_key`=" +
        Quote(conn.get(), user_key) + " THEN `peer_key` ELSE `owner_key` END AS `peer_key`"
        " FROM `friend_relation` WHERE (`owner_key`=" +
        Quote(conn.get(), user_key) + " OR `peer_key`=" + Quote(conn.get(), user_key) +
        ") AND `status`=2"
        ") x LEFT JOIN `health_profile` p ON p.`agent_uid`=x.`peer_key`"
        " LEFT JOIN `account` a ON a.`user_key`=x.`peer_key`"
        " WHERE x.`peer_key`<>" +
        Quote(conn.get(), user_key) + " ORDER BY x.`peer_key` ASC";

    if (mysql_query(conn.get(), sql.c_str()) != 0) return Lookup::Unavailable;
    MYSQL_RES* result = mysql_store_result(conn.get());
    if (result == nullptr) return Lookup::Unavailable;

    std::string previous;
    MYSQL_ROW row = nullptr;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        unsigned long* lengths = mysql_fetch_lengths(result);
        FriendMember member;
        member.user_key = Cell(row, lengths, 0);
        member.name = Cell(row, lengths, 1);
        if (member.user_key.empty() || member.user_key == previous) continue;
        previous = member.user_key;
        out.push_back(member);
    }
    mysql_free_result(result);
    return Lookup::Found;
}

FriendDao::Lookup FriendDao::LoadRelation(const std::string& owner_key,
                                          const std::string& peer_key,
                                          FriendRelation& out) const {
    db::DbConn conn(pool_);
    if (!conn) return Lookup::Unavailable;
    const std::string sql =
        "SELECT `status`,`blocked_by` FROM `friend_relation` WHERE `owner_key`=" +
        Quote(conn.get(), owner_key) + " AND `peer_key`=" + Quote(conn.get(), peer_key) +
        " LIMIT 1";
    if (mysql_query(conn.get(), sql.c_str()) != 0) return Lookup::Unavailable;
    MYSQL_RES* result = mysql_store_result(conn.get());
    if (result == nullptr) return Lookup::Unavailable;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row == nullptr) {
        mysql_free_result(result);
        out = FriendRelation{};
        return Lookup::NotFound;
    }
    unsigned long* lengths = mysql_fetch_lengths(result);
    out.found = true;
    out.status = std::atoi(Cell(row, lengths, 0).c_str());
    out.blocked_by = Cell(row, lengths, 1);
    mysql_free_result(result);
    return Lookup::Found;
}

bool FriendDao::UpsertRelation(const std::string& owner_key, const std::string& peer_key,
                               int status, const std::string& blocked_by) {
    db::DbConn conn(pool_);
    if (!conn) return false;
    const std::string blocked =
        blocked_by.empty() ? "NULL" : Quote(conn.get(), blocked_by);
    const std::string sql =
        "INSERT INTO `friend_relation` (`owner_key`,`peer_key`,`status`,`blocked_by`) VALUES (" +
        Quote(conn.get(), owner_key) + "," + Quote(conn.get(), peer_key) + "," +
        std::to_string(status) + "," + blocked +
        ") ON DUPLICATE KEY UPDATE `status`=VALUES(`status`),"
        "`blocked_by`=VALUES(`blocked_by`)";
    return mysql_query(conn.get(), sql.c_str()) == 0;
}

bool FriendDao::DeleteRelation(const std::string& owner_key, const std::string& peer_key) {
    db::DbConn conn(pool_);
    if (!conn) return false;
    const std::string sql = "DELETE FROM `friend_relation` WHERE `owner_key`=" +
                            Quote(conn.get(), owner_key) + " AND `peer_key`=" +
                            Quote(conn.get(), peer_key);
    return mysql_query(conn.get(), sql.c_str()) == 0;
}

bool FriendDao::DeleteBoth(const std::string& left_key, const std::string& right_key) {
    db::DbConn conn(pool_);
    if (!conn) return false;
    const std::string sql =
        "DELETE FROM `friend_relation` WHERE (`owner_key`=" + Quote(conn.get(), left_key) +
        " AND `peer_key`=" + Quote(conn.get(), right_key) + ") OR (`owner_key`=" +
        Quote(conn.get(), right_key) + " AND `peer_key`=" + Quote(conn.get(), left_key) +
        ")";
    return mysql_query(conn.get(), sql.c_str()) == 0;
}

}  // namespace eat::business
