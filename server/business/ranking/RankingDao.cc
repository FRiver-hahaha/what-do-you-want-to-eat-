#include "server/business/ranking/RankingDao.h"

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

RankingDao::Lookup RankingDao::ListFriends(const std::string& user_key,
                                           std::vector<RankingMember>& out) const {
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
        Quote(conn.get(), user_key) + " ORDER BY 1 ASC";

    if (mysql_query(conn.get(), sql.c_str()) != 0) return Lookup::Unavailable;
    MYSQL_RES* result = mysql_store_result(conn.get());
    if (result == nullptr) return Lookup::Unavailable;
    std::string previous;
    MYSQL_ROW row = nullptr;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        unsigned long* lengths = mysql_fetch_lengths(result);
        RankingMember member;
        member.user_key = Cell(row, lengths, 0);
        member.name = Cell(row, lengths, 1);
        if (member.user_key.empty() || member.user_key == previous) continue;
        previous = member.user_key;
        out.push_back(member);
    }
    mysql_free_result(result);
    return Lookup::Found;
}

}  // namespace eat::business
