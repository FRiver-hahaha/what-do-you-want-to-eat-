#include "http/dao/RankingDao.h"

#include <mysql/mysql.h>

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

std::string cell(MYSQL_ROW row, unsigned long* lengths, int index) {
    if (row[index] == nullptr) {
        return std::string();
    }
    return std::string(row[index], static_cast<std::size_t>(lengths[index]));
}

}  // namespace

RankingDao::Lookup RankingDao::listFriends(
    const std::string& agentUid, std::vector<RankingMember>& out) const {
    out.clear();
    DbConn conn(m_pool);
    if (!conn) {
        return Lookup::Unavailable;
    }

    // 展示名优先取 health_profile.name，缺失时回退 user.name。
    const std::string sql =
        "SELECT u2.`agent_uid`, COALESCE(NULLIF(p.`name`,''), u2.`name`) "
        "FROM `friend` f "
        "JOIN `user` u1 ON u1.`id`=f.`user_id` "
        "JOIN `user` u2 ON u2.`id`=f.`friend_id` "
        "LEFT JOIN `health_profile` p ON p.`agent_uid`=u2.`agent_uid` "
        "WHERE u1.`agent_uid`=" +
        quote(conn.get(), agentUid) +
        " AND f.`status`=2 AND u2.`agent_uid` IS NOT NULL "
        "UNION "
        "SELECT u2.`agent_uid`, COALESCE(NULLIF(p.`name`,''), u2.`name`) "
        "FROM `friend` f "
        "JOIN `user` u1 ON u1.`id`=f.`friend_id` "
        "JOIN `user` u2 ON u2.`id`=f.`user_id` "
        "LEFT JOIN `health_profile` p ON p.`agent_uid`=u2.`agent_uid` "
        "WHERE u1.`agent_uid`=" +
        quote(conn.get(), agentUid) +
        " AND f.`status`=2 AND u2.`agent_uid` IS NOT NULL "
        "ORDER BY 1 ASC";

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
        RankingMember member;
        member.agentUid = cell(row, lengths, 0);
        member.name = cell(row, lengths, 1);
        if (!member.agentUid.empty() && member.agentUid != agentUid) {
            out.push_back(member);
        }
    }
    mysql_free_result(result);
    return Lookup::Found;
}