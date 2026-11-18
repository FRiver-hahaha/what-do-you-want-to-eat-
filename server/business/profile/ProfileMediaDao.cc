#include "server/business/profile/ProfileMediaDao.h"

#include <mysql/mysql.h>

#include <chrono>
#include <cstdlib>
#include <string>

#include "server/database/DbConn.h"

namespace eat::business {
namespace {

std::string Cell(MYSQL_ROW row, unsigned long* lengths, int index) {
    if (row == nullptr || row[index] == nullptr) return std::string();
    return std::string(row[index], static_cast<std::size_t>(lengths[index]));
}

std::uint64_t ToVersion(const std::string& text) {
    try {
        return static_cast<std::uint64_t>(std::stoull(text));
    } catch (...) {
        return 0;
    }
}

std::uint64_t NextVersion(std::uint64_t previous) {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    const std::uint64_t candidate = static_cast<std::uint64_t>(now);
    return candidate > previous ? candidate : previous + 1;
}

}  // namespace

ProfileMediaDao::Lookup ProfileMediaDao::Load(const std::string& user_key,
                                              ProfileMediaRow& out) const {
    out = ProfileMediaRow{};
    db::DbConn conn(pool_);
    if (!conn) return Lookup::Unavailable;
    const std::string sql =
        "SELECT `cover_src`,`avatar_src`,`cover_ver`,`avatar_ver` FROM `profile_media`"
        " WHERE `user_key`='" + conn.escape(user_key) + "' LIMIT 1";
    if (mysql_query(conn.get(), sql.c_str()) != 0) return Lookup::Unavailable;
    MYSQL_RES* result = mysql_store_result(conn.get());
    if (result == nullptr) return Lookup::Unavailable;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row == nullptr) {
        mysql_free_result(result);
        return Lookup::NotFound;
    }
    unsigned long* lengths = mysql_fetch_lengths(result);
    out.cover = Cell(row, lengths, 0);
    out.avatar = Cell(row, lengths, 1);
    out.cover_ver = ToVersion(Cell(row, lengths, 2));
    out.avatar_ver = ToVersion(Cell(row, lengths, 3));
    mysql_free_result(result);
    return Lookup::Found;
}

ProfileMediaDao::Lookup ProfileMediaDao::Save(const std::string& user_key,
                                              const std::string* cover,
                                              const std::string* avatar,
                                              ProfileMediaRow& out) {
    const Lookup loaded = Load(user_key, out);
    if (loaded == Lookup::Unavailable) return Lookup::Unavailable;

    db::DbConn conn(pool_);
    if (!conn) return Lookup::Unavailable;

    if (cover != nullptr) {
        out.cover = *cover;
        out.cover_ver = NextVersion(out.cover_ver);
    }
    if (avatar != nullptr) {
        out.avatar = *avatar;
        out.avatar_ver = NextVersion(out.avatar_ver);
    }

    const std::string sql =
        "INSERT INTO `profile_media` "
        "(`user_key`,`cover_src`,`avatar_src`,`cover_ver`,`avatar_ver`) VALUES ('" +
        conn.escape(user_key) + "','" + conn.escape(out.cover) + "','" +
        conn.escape(out.avatar) + "'," + std::to_string(out.cover_ver) + "," +
        std::to_string(out.avatar_ver) +
        ") AS new ON DUPLICATE KEY UPDATE "
        "`cover_src`=new.`cover_src`,`avatar_src`=new.`avatar_src`,"
        "`cover_ver`=new.`cover_ver`,`avatar_ver`=new.`avatar_ver`";
    if (mysql_query(conn.get(), sql.c_str()) != 0) return Lookup::Unavailable;
    return Lookup::Found;
}

bool ProfileMediaDao::IsFriend(const std::string& owner_key,
                               const std::string& peer_key) const {
    db::DbConn conn(pool_);
    if (!conn) return false;
    const std::string owner = "'" + conn.escape(owner_key) + "'";
    const std::string peer = "'" + conn.escape(peer_key) + "'";
    const std::string sql =
        "SELECT 1 FROM `friend_relation` WHERE `status`=2 AND ("
        "(`owner_key`=" + owner + " AND `peer_key`=" + peer + ") OR "
        "(`owner_key`=" + peer + " AND `peer_key`=" + owner + ")) LIMIT 1";
    if (mysql_query(conn.get(), sql.c_str()) != 0) return false;
    MYSQL_RES* result = mysql_store_result(conn.get());
    if (result == nullptr) return false;
    const bool found = mysql_fetch_row(result) != nullptr;
    mysql_free_result(result);
    return found;
}

}  // namespace eat::business
