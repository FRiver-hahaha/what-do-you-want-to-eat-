#include "http/dao/UserDao.h"

#include <mysql/mysql.h>

#include "database/MySQLPool.h"
#include "http/DbConn.h"

UserDao::Lookup UserDao::findByAgentUid(const std::string& agentUid) const {
    DbConn conn(m_pool);
    if (!conn) {
        return Lookup::Unavailable;
    }

    const std::string sql = "SELECT 1 FROM `user` WHERE `agent_uid`='" +
                            conn.escape(agentUid) + "' LIMIT 1";
    if (mysql_query(conn.get(), sql.c_str()) != 0) {
        return Lookup::Unavailable;
    }
    MYSQL_RES* result = mysql_store_result(conn.get());
    if (result == nullptr) {
        return Lookup::Unavailable;
    }
    MYSQL_ROW row = mysql_fetch_row(result);
    mysql_free_result(result);
    return row != nullptr ? Lookup::Found : Lookup::NotFound;
}