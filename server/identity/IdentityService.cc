#include "server/identity/IdentityService.h"

#include <mysql/mysql.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <vector>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include "server/database/DbConn.h"
#include "server/identity/PasswordHasher.h"

namespace eat::identity {
namespace {

constexpr int kMaxFailedAttempts = 5;

std::string Cell(MYSQL_ROW row, unsigned long* lengths, int index) {
    if (row == nullptr || row[index] == nullptr) return std::string();
    return std::string(row[index], static_cast<std::size_t>(lengths[index]));
}

std::string NormalizeLoginId(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        out.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    }
    return out;
}

bool RandomBytes(unsigned char* buffer, std::size_t size) {
    return RAND_bytes(buffer, static_cast<int>(size)) == 1;
}

std::string Base64Url(const unsigned char* data, std::size_t size) {
    std::string encoded(static_cast<std::size_t>(((size + 2) / 3) * 4) + 1, '\0');
    const int written = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&encoded[0]), data,
                                        static_cast<int>(size));
    encoded.resize(static_cast<std::size_t>(written));
    for (char& c : encoded) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!encoded.empty() && encoded.back() == '=') encoded.pop_back();
    return encoded;
}

std::string Sha256Hex(const std::string& text) {
    unsigned char digest[EVP_MAX_MD_SIZE] = {};
    unsigned int length = 0;
    EVP_Digest(text.data(), text.size(), digest, &length, EVP_sha256(), nullptr);
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(length * 2);
    for (unsigned int i = 0; i < length; ++i) {
        out.push_back(digits[digest[i] >> 4]);
        out.push_back(digits[digest[i] & 0x0F]);
    }
    return out;
}

}  // namespace

std::string NewUserKey() {
    // ULID-compatible: 48-bit big-endian millisecond timestamp + 80 random bits,
    // rendered as 26 Crockford base32 characters.
    static const char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    unsigned char bytes[16] = {};
    const auto now_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    for (int i = 5; i >= 0; --i) {
        bytes[i] = static_cast<unsigned char>(now_ms >> (8 * (5 - i)));
    }
    if (!RandomBytes(bytes + 6, 10)) return std::string();
    std::string out(26, '0');
    // 130 bits are encoded into 26 groups of 5; the first group uses the top
    // 3 bits of the 128-bit value, matching the ULID layout.
    std::size_t bit = 128;
    for (std::size_t i = 0; i < 26; ++i) {
        std::uint32_t value = 0;
        for (int b = 0; b < 5; ++b) {
            value <<= 1;
            if (bit == 0) continue;
            --bit;
            const std::size_t byte_index = bit / 8;
            const std::size_t bit_index = 7 - (bit % 8);
            value |= (bytes[byte_index] >> bit_index) & 1u;
        }
        out[i] = kAlphabet[value & 0x1F];
    }
    return out;
}

std::string NewOpaqueToken() {
    unsigned char bytes[32] = {};
    if (!RandomBytes(bytes, sizeof(bytes))) return std::string();
    return Base64Url(bytes, sizeof(bytes));
}

IdentityStatus IdentityService::Register(const std::string& login_id,
                                         const std::string& password,
                                         const std::string& display_name,
                                         LoginTokens& out) {
    const std::string normalized = NormalizeLoginId(login_id);
    if (normalized.size() < 3 || normalized.size() > 128 || password.size() < 8 ||
        password.size() > 256) {
        return IdentityStatus::InvalidParams;
    }
    const std::string user_key = NewUserKey();
    if (user_key.size() != 26) return IdentityStatus::Unavailable;
    const std::string digest = PasswordHasher::Hash(password);
    if (digest.empty()) return IdentityStatus::Unavailable;

    db::DbConn conn(pool_);
    if (!conn) return IdentityStatus::Unavailable;
    const std::string sql =
        "INSERT INTO `account` (`user_key`,`login_id`,`display_name`,`password_digest`) VALUES ('" +
        conn.escape(user_key) + "','" + conn.escape(normalized) + "','" +
        conn.escape(display_name) + "','" + conn.escape(digest) + "')";
    if (mysql_query(conn.get(), sql.c_str()) != 0) {
        return mysql_errno(conn.get()) == 1062 ? IdentityStatus::DuplicateLoginId
                                               : IdentityStatus::Unavailable;
    }
    out.user_key = user_key;
    out.account_id = static_cast<std::uint64_t>(mysql_insert_id(conn.get()));
    return IdentityStatus::Ok;
}

IdentityStatus IdentityService::Login(const std::string& login_id,
                                      const std::string& password,
                                      const std::string& device_id,
                                      const std::string& client_version,
                                      LoginTokens& out) {
    const std::string normalized = NormalizeLoginId(login_id);
    if (normalized.empty() || password.empty()) return IdentityStatus::InvalidCredentials;

    db::DbConn conn(pool_);
    if (!conn) return IdentityStatus::Unavailable;

    const std::string select =
        "SELECT `account_id`,`user_key`,`password_digest`,`status`,"
        "(`locked_until` IS NOT NULL AND `locked_until` > NOW()) AS locked "
        "FROM `account` WHERE `login_id`='" + conn.escape(normalized) + "' LIMIT 1";
    if (mysql_query(conn.get(), select.c_str()) != 0) return IdentityStatus::Unavailable;
    MYSQL_RES* result = mysql_store_result(conn.get());
    if (result == nullptr) return IdentityStatus::Unavailable;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row == nullptr) {
        mysql_free_result(result);
        return IdentityStatus::InvalidCredentials;
    }
    unsigned long* lengths = mysql_fetch_lengths(result);
    const std::uint64_t account_id = std::strtoull(Cell(row, lengths, 0).c_str(), nullptr, 10);
    const std::string user_key = Cell(row, lengths, 1);
    const std::string digest = Cell(row, lengths, 2);
    const int status = std::atoi(Cell(row, lengths, 3).c_str());
    const bool locked = Cell(row, lengths, 4) == "1";
    mysql_free_result(result);

    if (locked) return IdentityStatus::Locked;
    if (status != 1) return IdentityStatus::Disabled;
    if (!PasswordHasher::Verify(password, digest)) {
        const std::string update =
            "UPDATE `account` SET `failed_attempts`=`failed_attempts`+1,"
            "`locked_until`=CASE WHEN `failed_attempts`+1>=" +
            std::to_string(kMaxFailedAttempts) +
            " THEN DATE_ADD(NOW(), INTERVAL 5 MINUTE) ELSE `locked_until` END "
            "WHERE `account_id`=" + std::to_string(account_id);
        mysql_query(conn.get(), update.c_str());
        return IdentityStatus::InvalidCredentials;
    }
    const std::string reset =
        "UPDATE `account` SET `failed_attempts`=0,`locked_until`=NULL,"
        "`last_login_at`=NOW() WHERE `account_id`=" + std::to_string(account_id);
    mysql_query(conn.get(), reset.c_str());

    const std::string access = NewOpaqueToken();
    const std::string refresh = NewOpaqueToken();
    if (access.empty() || refresh.empty()) return IdentityStatus::Unavailable;
    const std::string access_hash = Sha256Hex(access);
    const std::string refresh_hash = Sha256Hex(refresh);
    const std::string insert =
        "INSERT INTO `session` (`account_id`,`session_hash`,`refresh_hash`,"
        "`access_expires_at`,`refresh_expires_at`,`device_id`,`client_version`) VALUES (" +
        std::to_string(account_id) + ",'" + access_hash + "','" + refresh_hash +
        "',DATE_ADD(NOW(), INTERVAL 30 MINUTE),DATE_ADD(NOW(), INTERVAL 30 DAY),'" +
        conn.escape(device_id) + "','" + conn.escape(client_version) + "')";
    if (mysql_query(conn.get(), insert.c_str()) != 0) return IdentityStatus::Unavailable;

    out.user_key = user_key;
    out.account_id = account_id;
    out.access_token = access;
    out.refresh_token = refresh;
    return IdentityStatus::Ok;
}

bool IdentityService::Logout(const std::string& access_token, std::string& error) {
    if (access_token.empty()) {
        error = "missing_token";
        return false;
    }
    db::DbConn conn(pool_);
    if (!conn) {
        error = "database_unavailable";
        return false;
    }
    const std::string sql =
        "UPDATE `session` SET `status`=0,`revoked_at`=NOW(),`revoke_reason`='logout' "
        "WHERE `session_hash`='" + Sha256Hex(access_token) + "' AND `status`=1";
    if (mysql_query(conn.get(), sql.c_str()) != 0) {
        error = "database_error";
        return false;
    }
    return mysql_affected_rows(conn.get()) > 0;
}

bool IdentityService::UserExists(const std::string& user_key) {
    if (user_key.empty()) return false;
    db::DbConn conn(pool_);
    if (!conn) return false;
    const std::string sql = "SELECT 1 FROM `account` WHERE `user_key`='" +
                            conn.escape(user_key) + "' AND `status`=1 LIMIT 1";
    if (mysql_query(conn.get(), sql.c_str()) != 0) return false;
    MYSQL_RES* result = mysql_store_result(conn.get());
    if (result == nullptr) return false;
    const bool found = mysql_fetch_row(result) != nullptr;
    mysql_free_result(result);
    return found;
}

bool IdentityService::LoadBySessionHash(const std::string& session_hash,
                                        SessionRecord& out) {
    db::DbConn conn(pool_);
    if (!conn) return false;
    const std::string sql =
        "SELECT a.`user_key`, s.`session_id`, s.`account_id`, "
        "IF(s.`status`=1 AND a.`status`=1, 1, 0) AS active, "
        "UNIX_TIMESTAMP(s.`access_expires_at`)*1000, a.`revoke_epoch` "
        "FROM `session` s JOIN `account` a ON a.`account_id`=s.`account_id` "
        "WHERE s.`session_hash`='" + conn.escape(session_hash) + "' LIMIT 1";
    if (mysql_query(conn.get(), sql.c_str()) != 0) return false;
    MYSQL_RES* result = mysql_store_result(conn.get());
    if (result == nullptr) return false;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row == nullptr) {
        mysql_free_result(result);
        return false;
    }
    unsigned long* lengths = mysql_fetch_lengths(result);
    out.user_key = Cell(row, lengths, 0);
    out.session_id = Cell(row, lengths, 1);
    out.account_id = std::strtoull(Cell(row, lengths, 2).c_str(), nullptr, 10);
    out.status = std::atoi(Cell(row, lengths, 3).c_str());
    out.access_expires_at_ms = std::atoll(Cell(row, lengths, 4).c_str());
    out.revoke_epoch = static_cast<std::uint32_t>(
        std::strtoul(Cell(row, lengths, 5).c_str(), nullptr, 10));
    out.account_revoke_epoch = out.revoke_epoch;
    mysql_free_result(result);
    return true;
}

}  // namespace eat::identity