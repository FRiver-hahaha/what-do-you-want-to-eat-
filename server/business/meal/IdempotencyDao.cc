#include "server/business/meal/IdempotencyDao.h"

#include <cstdlib>

namespace eat::business {
namespace {

constexpr unsigned int kErrDupEntry = 1062;

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

IdempotencyDao::Lookup IdempotencyDao::FindOn(MYSQL* conn, const std::string& user_key,
                                              const std::string& idem_key,
                                              IdempotencyRecord& out) {
    const std::string sql =
        "SELECT `payload_hash`,`meal_id` FROM `idempotency_records` "
        "WHERE `user_key`=" +
        Quote(conn, user_key) + " AND `idem_key`=" + Quote(conn, idem_key) +
        " AND `created_day`=CURDATE() LIMIT 1";
    if (mysql_query(conn, sql.c_str()) != 0) return Lookup::Unavailable;
    MYSQL_RES* result = mysql_store_result(conn);
    if (result == nullptr) return Lookup::Unavailable;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row == nullptr) {
        mysql_free_result(result);
        return Lookup::NotFound;
    }
    unsigned long* lengths = mysql_fetch_lengths(result);
    out.payload_hash = Cell(row, lengths, 0);
    out.meal_id = std::strtoull(Cell(row, lengths, 1).c_str(), nullptr, 10);
    mysql_free_result(result);
    return Lookup::Found;
}

IdempotencyDao::Insert IdempotencyDao::InsertOn(MYSQL* conn, const std::string& user_key,
                                                const std::string& idem_key,
                                                const std::string& payload_hash,
                                                unsigned long long meal_id) {
    const std::string sql =
        "INSERT INTO `idempotency_records` "
        "(`agent_uid`,`user_key`,`idem_key`,`payload_hash`,`meal_id`,`created_at`) "
        "VALUES (" +
        Quote(conn, user_key) + "," + Quote(conn, user_key) + "," +
        Quote(conn, idem_key) + "," + Quote(conn, payload_hash) + "," +
        std::to_string(meal_id) + ",NOW())";
    if (mysql_query(conn, sql.c_str()) == 0) return Insert::Ok;
    if (mysql_errno(conn) == kErrDupEntry) return Insert::Duplicate;
    return Insert::Unavailable;
}

}  // namespace eat::business
