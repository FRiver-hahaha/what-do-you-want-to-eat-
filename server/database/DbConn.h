#pragma once

#include <mysql/mysql.h>

#include <string>

#include "server/database/MySQLPool.h"

namespace eat::db {

// RAII session over MySQLPool: borrows with a bounded timeout and always
// returns the connection. A null session means "database unavailable"; callers
// must map that to 5001 instead of dereferencing it.
class DbConn {
 public:
    static constexpr int kDefaultTimeoutMs = 200;

    explicit DbConn(MySQLPool& pool, int timeout_ms = kDefaultTimeoutMs)
        : pool_(pool), connection_(pool.Borrow(timeout_ms)) {}

    ~DbConn() {
        if (connection_ != nullptr) pool_.Return(connection_);
    }

    DbConn(const DbConn&) = delete;
    DbConn& operator=(const DbConn&) = delete;

    MYSQL* get() const { return connection_; }
    explicit operator bool() const { return connection_ != nullptr; }

    std::string escape(const std::string& input) const {
        if (connection_ == nullptr) return std::string();
        std::string out(input.size() * 2 + 1, '\0');
        const unsigned long n = mysql_real_escape_string(
            connection_, &out[0], input.c_str(), input.size());
        out.resize(n);
        return out;
    }

 private:
    MySQLPool& pool_;
    MYSQL* connection_ = nullptr;
};

class DbTransaction {
 public:
    explicit DbTransaction(DbConn& conn) : conn_(conn) {
        if (static_cast<bool>(conn_)) {
            active_ = mysql_autocommit(conn_.get(), 0) == 0;
            autocommit_set_ = true;
        }
    }

    ~DbTransaction() {
        if (active_) mysql_rollback(conn_.get());
        restore();
    }

    DbTransaction(const DbTransaction&) = delete;
    DbTransaction& operator=(const DbTransaction&) = delete;

    bool active() const { return active_; }

    bool commit() {
        if (!active_) return false;
        active_ = false;
        const bool ok = mysql_commit(conn_.get()) == 0;
        restore();
        return ok;
    }

    void rollback() {
        if (!active_) return;
        active_ = false;
        mysql_rollback(conn_.get());
        restore();
    }

 private:
    void restore() {
        if (autocommit_set_) {
            mysql_autocommit(conn_.get(), 1);
            autocommit_set_ = false;
        }
    }

    DbConn& conn_;
    bool active_ = false;
    bool autocommit_set_ = false;
};

}  // namespace eat::db
