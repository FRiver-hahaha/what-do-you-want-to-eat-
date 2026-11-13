#include "MySQLPool.h"

MySQLPool::MySQLPool() {}

MySQLPool::~MySQLPool() {
    while (!m_pool.empty()) {
        MYSQL* conn = m_pool.front();
        m_pool.pop();
        mysql_close(conn);
    }
}

bool MySQLPool::init(int size,
                     const std::string& user,
                     const std::string& passwd,
                     const std::string& db,
                     const std::string& ip,
                     unsigned short port) {
    for (int i = 0; i < size; i++) {
        MYSQL* conn = mysql_init(nullptr);
        if (!conn) {
            return false;
        }
        mysql_set_character_set(conn, "utf8mb4");
        if (!mysql_real_connect(conn, ip.c_str(), user.c_str(), passwd.c_str(),
                                db.c_str(), port, nullptr, 0)) {
            mysql_close(conn);
            return false;
        }
        m_pool.push(conn);
    }
    return true;
}

MYSQL* MySQLPool::borrow() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return !m_pool.empty(); });
    MYSQL* conn = m_pool.front();
    m_pool.pop();
    return conn;
}

void MySQLPool::returnConn(MYSQL* conn) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool.push(conn);
    m_cv.notify_one();
}