#include "GroupModel.h"

GroupModel::GroupModel(MySQLPool* pool) {
    m_mysql.setPool(pool);
}

bool GroupModel::createGroup(std::string name, int owner_id, int& group_id) {
    MYSQL* conn;
    if ((conn = m_mysql.transaction()) == nullptr) {
        return false;
    }
    std::string sql = "INSERT INTO group_info(name, owner_id) VALUES('" +
                      m_mysql.escape(conn, name) + "'," +
                      std::to_string(owner_id) + ");";
    if (mysql_query(conn, sql.c_str()) == 0) {
        group_id = static_cast<int>(mysql_insert_id(conn));
        std::string sql2 =
            "INSERT INTO group_user(group_id,user_id,role) VALUES(" +
            std::to_string(group_id) + ", " + std::to_string(owner_id) +
            ", 3);";
        if (mysql_query(conn, sql2.c_str()) == 0 && m_mysql.commit(conn)) {
            return true;
        }
    }
    m_mysql.rollback(conn);
    return false;
}

std::vector<json> GroupModel::queryGroups(int user_id) {
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "SELECT g.id, g.name "
             "FROM group_info g "
             "INNER JOIN group_user gu "
             "ON g.id = gu.group_id "
             "WHERE gu.user_id = %d "
             "AND gu.role > 0;",
             user_id);
    std::vector<json> groups;
    std::vector<std::vector<std::string>> rows;
    if (!m_mysql.queryAll(sql, rows)) {
        return groups;
    }
    for (auto& row : rows) {
        json group;
        group["id"] = std::stoi(row[0]);
        group["name"] = row[1];
        groups.emplace_back(group);
    }
    return groups;
}

std::vector<std::pair<int, int>> GroupModel::queryMembers(int group_id) {
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "SELECT user_id, role FROM group_user "
             "WHERE group_id = %d;",
             group_id);
    std::vector<std::pair<int, int>> members;
    std::vector<std::vector<std::string>> rows;
    if (!m_mysql.queryAll(sql, rows)) {
        return members;
    }
    for (auto& row : rows) {
        members.emplace_back(std::stoi(row[0]), std::stoi(row[1]));
    }
    return members;
}

bool GroupModel::groupExist(int group_id) {
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "SELECT 1 FROM group_info "
             "WHERE id = %d;",
             group_id);
    std::vector<std::vector<std::string>> rows;
    return m_mysql.queryAll(sql, rows) && !rows.empty();
}

std::string GroupModel::getGroupName(int group_id) {
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql), "SELECT name FROM group_info WHERE id = %d;",
             group_id);
    std::vector<std::vector<std::string>> rows;
    if (!m_mysql.queryAll(sql, rows) || rows.empty()) {
        return "";
    }
    return rows[0][0];
}

bool GroupModel::isInGroup(int group_id, int user_id) {
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "SELECT user_id FROM group_user "
             "WHERE group_id = %d AND user_id = %d",
             group_id, user_id);
    std::vector<std::vector<std::string>> rows;
    return m_mysql.queryAll(sql, rows) && !rows.empty();
}

bool GroupModel::applyGroup(int group_id,
                            int user_id,
                            std::vector<int>& users) {
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "INSERT INTO group_user(group_id, user_id, role) "
             "VALUES(%d, %d, 0);",
             group_id, user_id);
    if (!m_mysql.update(sql)) {
        return false;
    }
    snprintf(sql, sizeof(sql),
             "SELECT user_id FROM group_user "
             "WHERE group_id = %d AND role > 1",
             group_id);
    std::vector<std::vector<std::string>> rows;
    if (!m_mysql.queryAll(sql, rows)) {
        return false;
    }
    for (auto& row : rows) {
        users.emplace_back(std::stoi(row[0]));
    }
    return true;
}

bool GroupModel::processGroupRequest(int group_id, int user_id, bool agree) {
    char sql[1024] = {0};
    if (agree) {
        snprintf(sql, sizeof(sql),
                 "UPDATE group_user SET role = 1 "
                 "WHERE group_id = %d AND user_id = %d;",
                 group_id, user_id);
    } else {
        snprintf(sql, sizeof(sql),
                 "DELETE FROM group_user "
                 "WHERE group_id = %d AND user_id = %d;",
                 group_id, user_id);
    }
    return m_mysql.update(sql);
}

bool GroupModel::addMember(int group_id, int user_id, int role) {
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "INSERT INTO group_user(group_id, user_id, role) "
             "VALUES(%d, %d, %d);",
             group_id, user_id, role);
    return m_mysql.update(sql);
}

bool GroupModel::delMember(int group_id, int user_id) {
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "DELETE FROM group_user "
             "WHERE group_id = %d AND user_id = %d;",
             group_id, user_id);
    return m_mysql.update(sql);
}

bool GroupModel::dissolveGroup(int group_id) {
    MYSQL* conn;
    if ((conn = m_mysql.transaction()) == nullptr) {
        return false;
    }
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql), "DELETE FROM group_user WHERE group_id = %d;",
             group_id);
    if (mysql_query(conn, sql)) {
        m_mysql.rollback(conn);
        return false;
    }
    snprintf(sql, sizeof(sql), "DELETE FROM group_info WHERE id = %d;",
             group_id);
    if (mysql_query(conn, sql) == 0 && m_mysql.commit(conn)) {
        return true;
    }
    m_mysql.rollback(conn);
    return false;
}

bool GroupModel::setRole(int group_id, int target_id, int role) {
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "UPDATE group_user SET role = %d "
             "WHERE group_id = %d AND user_id = %d;",
             role, group_id, target_id);
    return m_mysql.update(sql);
}

int GroupModel::getRole(int group_id, int user_id) {
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "SELECT role FROM group_user "
             "WHERE group_id = %d AND user_id = %d;",
             group_id, user_id);
    std::vector<std::vector<std::string>> rows;
    if (m_mysql.queryAll(sql, rows) && !rows.empty()) {
        return std::stoi(rows[0][0]);
    }
    return -1;
}

bool GroupModel::removeFromAll(int user_id) {
    char sql[1024] = {0};
    // 删除属于该用户创建的群（先删成员再删群信息）
    snprintf(sql, sizeof(sql),
             "DELETE gu FROM group_user gu "
             "INNER JOIN group_info g ON gu.group_id = g.id "
             "WHERE g.owner_id = %d;",
             user_id);
    m_mysql.update(sql);
    snprintf(sql, sizeof(sql), "DELETE FROM group_info WHERE owner_id = %d;",
             user_id);
    m_mysql.update(sql);
    // 删除该用户在其他群中的成员记录
    snprintf(sql, sizeof(sql), "DELETE FROM group_user WHERE user_id = %d;",
             user_id);
    return m_mysql.update(sql);
}