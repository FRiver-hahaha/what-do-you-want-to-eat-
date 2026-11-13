#pragma once

#include "../database/MySQL.h"
#include "User.h"

class MySQLPool;

class UserModel {
   public:
    UserModel(MySQLPool* pool);
    bool insert(User& user);
    bool queryById(int id, User& user);
    bool queryByEmail(const std::string& email, User& user);
    bool updateState(int id, int state);
    // 原子地把 state 从 0 抢为 1：并发登录时只有一个请求能成功。
    bool tryLogin(int id);
    bool resetState();
    bool updatePassword(int id, const std::string& password);
    bool remove(int id);

   private:
    MySQL m_mysql;
};