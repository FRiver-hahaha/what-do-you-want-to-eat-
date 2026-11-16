#pragma once

#include <mysql/mysql.h>

#include <string>

#include "database/MySQLPool.h"

// HTTP 进程的数据库会话：RAII 借还 + 限时借用。
// borrow 失败（池耗尽 / DB 不可用）时 get() 返回 nullptr，调用方必须显式处理。
class DbConn {
   public:
    static constexpr int kTimeoutMs = 3000;

    explicit DbConn(MySQLPool& pool, int timeoutMs = kTimeoutMs)
        : m_pool(pool), m_conn(pool.borrow_for(timeoutMs)) {}

    ~DbConn() {
        if (m_conn != nullptr) {
            m_pool.returnConn(m_conn);
        }
    }

    DbConn(const DbConn&) = delete;
    DbConn& operator=(const DbConn&) = delete;

    MYSQL* get() const { return m_conn; }
    explicit operator bool() const { return m_conn != nullptr; }

    std::string escape(const std::string& input) const {
        if (m_conn == nullptr) {
            return std::string();
        }
        std::string out(input.size() * 2 + 1, '\0');
        const unsigned long n = mysql_real_escape_string(
            m_conn, &out[0], input.c_str(), input.size());
        out.resize(n);
        return out;
    }

   private:
    MySQLPool& m_pool;
    MYSQL* m_conn;
};
// 事务守卫：作用域内未 commit 则自动回滚，并始终把连接恢复为 autocommit。
// 只允许在单个借用连接上使用，禁止跨请求共享。
class DbTransaction {
   public:
    explicit DbTransaction(DbConn& conn) : m_conn(conn) {
        if (static_cast<bool>(m_conn)) {
            m_active = mysql_autocommit(m_conn.get(), 0) == 0;
            m_autocommitSet = true;
        }
    }

    ~DbTransaction() {
        if (m_active) {
            mysql_rollback(m_conn.get());
        }
        if (m_autocommitSet) {
            mysql_autocommit(m_conn.get(), 1);
        }
    }

    DbTransaction(const DbTransaction&) = delete;
    DbTransaction& operator=(const DbTransaction&) = delete;

    bool active() const { return m_active; }

    bool commit() {
        if (!m_active) {
            return false;
        }
        m_active = false;
        const bool ok = mysql_commit(m_conn.get()) == 0;
        restore();
        return ok;
    }

    void rollback() {
        if (!m_active) {
            return;
        }
        m_active = false;
        mysql_rollback(m_conn.get());
        restore();
    }

   private:
    void restore() {
        if (m_autocommitSet) {
            mysql_autocommit(m_conn.get(), 1);
            m_autocommitSet = false;
        }
    }

    DbConn& m_conn;
    bool m_active = false;
    bool m_autocommitSet = false;
};