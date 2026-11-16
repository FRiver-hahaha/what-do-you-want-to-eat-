#include "http/dao/IdempotencyDao.h"

#include <mysql/mysql.h>

#include <cstdlib>
#include <string>

namespace {

constexpr unsigned int kErrDupEntry = 1062;

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

IdempotencyDao::Lookup IdempotencyDao::findOn(MYSQL* conn,
                                              const std::string& agentUid,
                                              const std::string& idemKey,
                                              IdempotencyRecord& out) {
    const std::string sql =
        "SELECT `payload_hash`,`meal_id` FROM `idempotency_records` "
        "WHERE `agent_uid`=" +
        quote(conn, agentUid) + " AND `idem_key`=" + quote(conn, idemKey) +
        " LIMIT 1";
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
    out.payloadHash = cell(row, lengths, 0);
    out.mealId = std::strtoull(cell(row, lengths, 1).c_str(), nullptr, 10);
    mysql_free_result(result);
    return Lookup::Found;
}

IdempotencyDao::Insert IdempotencyDao::insertOn(MYSQL* conn,
                                                const std::string& agentUid,
                                                const std::string& idemKey,
                                                const std::string& payloadHash,
                                                unsigned long long mealId) {
    const std::string sql =
        "INSERT INTO `idempotency_records` "
        "(`agent_uid`,`idem_key`,`payload_hash`,`meal_id`) VALUES (" +
        quote(conn, agentUid) + "," + quote(conn, idemKey) + "," +
        quote(conn, payloadHash) + "," + std::to_string(mealId) + ")";
    if (mysql_query(conn, sql.c_str()) == 0) {
        return Insert::Ok;
    }
    if (mysql_errno(conn) == kErrDupEntry) {
        return Insert::Duplicate;
    }
    return Insert::Unavailable;
}