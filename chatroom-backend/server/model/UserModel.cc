#include <cstdio>
#include <string>
#include <vector>

#include "UserModel.h"

UserModel::UserModel(MySQLPool* pool) {
    m_mysql.setPool(pool);
}

bool UserModel::insert(User& user) {
    std::string sql =
        "INSERT INTO user(name, email, password, state) VALUES('" +
        m_mysql.escape(user.getName()) + "','" +
        m_mysql.escape(user.getEmail()) + "','" +
        m_mysql.escape(user.getPassword()) + "'," +
        std::to_string(user.getState()) + ");";
    return m_mysql.update(sql);
}

bool UserModel::queryById(int id, User& user) {
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "SELECT id, name, email, password, state FROM user "
             "WHERE id = %d;",
             id);
    std::vector<std::vector<std::string>> rows;
    if (!m_mysql.queryAll(sql, rows) || rows.empty()) {
        return false;
    }
    auto& row = rows[0];
    user.setId(std::stoi(row[0]));
    user.setName(row[1]);
    user.setEmail(row[2]);
    user.setPassword(row[3]);
    user.setState(std::stoi(row[4]));
    return true;
}

bool UserModel::queryByEmail(const std::string& email, User& user) {
    std::string sql =
        "SELECT id, name, email, password, state FROM user "
        "WHERE email = '" +
        m_mysql.escape(email) + "';";
    std::vector<std::vector<std::string>> rows;
    if (!m_mysql.queryAll(sql, rows) || rows.empty()) {
        return false;
    }
    auto& row = rows[0];
    user.setId(std::stoi(row[0]));
    user.setName(row[1]);
    user.setEmail(row[2]);
    user.setPassword(row[3]);
    user.setState(std::stoi(row[4]));
    return true;
}

bool UserModel::updateState(int id, int state) {
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "UPDATE user SET state = %d "
             "WHERE id = %d;",
             state, id);
    return m_mysql.update(sql);
}

bool UserModel::tryLogin(int id) {
    // 条件更新保证 CAS 语义：只有把 state 由 0 改成 1 的那次调用返回 1 行。
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "UPDATE user SET state = 1 WHERE id = %d AND state = 0;", id);
    return m_mysql.updateAffected(sql) == 1;
}

bool UserModel::resetState() {
    return m_mysql.update("UPDATE user SET state = 0;");
}

bool UserModel::updatePassword(int id, const std::string& password) {
    std::string sql = "UPDATE user SET password = '" +
                      m_mysql.escape(password) +
                      "' WHERE id = " + std::to_string(id) + ";";
    return m_mysql.update(sql);
}

bool UserModel::remove(int id) {
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql), "DELETE FROM user WHERE id = %d;", id);
    return m_mysql.update(sql);
}
