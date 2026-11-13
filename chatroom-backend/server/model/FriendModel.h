#pragma once

#include <vector>

#include "../database/MySQL.h"
#include "User.h"
#include "UserModel.h"

class MySQLPool;

class FriendModel {
   public:
    FriendModel(MySQLPool* pool);
    bool insert(int user_id, int friend_id);
    bool deleteFriend(int user_id, int friend_id);
    bool updateStatus(int user_id, int friend_id, int status);
    bool addFriend(int user_id, int friend_id);
    std::vector<User> queryFriends(int user_id);
    int isFriend(int user_id, int friend_id);
    bool removeAll(int user_id);

   private:
    MySQL m_mysql;
    UserModel m_user_model;
};