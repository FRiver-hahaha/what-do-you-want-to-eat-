#include "MySQLPool.h"

#include <chrono>
#include <thread>

namespace {
// 建连超时：数据库不可用时也必须让 borrow_for 在设定时间内返回。
constexpr unsigned int kConnectTimeoutSec = 3;
}  // namespace

MySQLPool::MySQLPool() {}

MySQLPool::~MySQLPool() {
    std::lock_guard<std::mutex> lock(m_mutex);
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
    m_user = user;
    m_passwd = passwd;
    m_db = db;
    m_ip = ip;
    m_port = port;
    m_capacity = size > 0 ? size : 1;

    for (int i = 0; i < m_capacity; ++i) {
        MYSQL* conn = createConn();
        if (conn == nullptr) {
            while (!m_pool.empty()) {
                mysql_close(m_pool.front());
                m_pool.pop();
            }
            m_live = 0;
            return false;
        }
        m_pool.push(conn);
        ++m_live;
    }
    return true;
}

MYSQL* MySQLPool::createConn() {
    MYSQL* conn = mysql_init(nullptr);
    if (conn == nullptr) {
        return nullptr;
    }
    unsigned int timeout = kConnectTimeoutSec;
    mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);


    // HOST 以 '/' 开头时视为本地 Unix socket 路径（部署可选，避免 TCP 暴露）。
    const char* host = m_ip.c_str();
    const char* socketPath = nullptr;
    std::string socketPathStore;
    if (!m_ip.empty() && m_ip.front() == '/') {
        socketPathStore = m_ip;
        host = nullptr;
        socketPath = socketPathStore.c_str();
    }

    if (mysql_real_connect(conn, host, m_user.c_str(), m_passwd.c_str(),
                           m_db.c_str(), m_port, socketPath, 0) == nullptr) {
        mysql_close(conn);
        return nullptr;
    }
    // 会话字符集必须在建连成功后设置，否则调用无效。
    mysql_set_character_set(conn, "utf8mb4");
    return conn;
}

MYSQL* MySQLPool::borrow() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return !m_pool.empty(); });
    MYSQL* conn = m_pool.front();
    m_pool.pop();
    return conn;
}

MYSQL* MySQLPool::borrow_for(int timeoutMs) {
    const int bounded = timeoutMs > 0 ? timeoutMs : 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(bounded);

    for (;;) {
        MYSQL* conn = nullptr;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            // 仅在"池内无空闲连接且已达容量上限"时等待归还；
            // 若曾有连接因断连被丢弃（m_live < m_capacity），直接进入补建，
            // 否则池被清空后将永久不可恢复。
            if (m_pool.empty() && m_live >= m_capacity) {
                if (m_cv.wait_until(lock, deadline) == std::cv_status::timeout &&
                    m_pool.empty() && m_live >= m_capacity) {
                    return nullptr;
                }
            }
            if (!m_pool.empty()) {
                conn = m_pool.front();
                m_pool.pop();
            }
        }

        if (conn != nullptr) {
            // 借出即探活：断连则丢弃并尝试补建，避免把坏连接交给业务层。
            if (mysql_ping(conn) == 0) {
                return conn;
            }
            mysql_close(conn);
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_live;
        }

        // 补建入口：先预占名额再建连，保证并发补建不超过容量。
        bool reserved = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_live < m_capacity) {
                ++m_live;
                reserved = true;
            }
        }
        if (!reserved) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return nullptr;
            }
            // 其他线程已补齐连接，回到循环取用。
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        MYSQL* rebuilt = createConn();
        if (rebuilt != nullptr) {
            return rebuilt;
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_live;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return nullptr;
        }
        // 数据库不可用时避免忙等，但始终受 deadline 约束。
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void MySQLPool::returnConn(MYSQL* conn) {
    if (conn == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pool.push(conn);
    }
    m_cv.notify_one();
}