#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

#include "../database/MySQL.h"

using json = nlohmann::json;

class MySQLPool;

class GroupModel {
   public:
    GroupModel(MySQLPool* pool);
    bool createGroup(std::string name, int owner_id, int& group_id);
    std::vector<json> queryGroups(int user_id);
    std::vector<std::pair<int, int>> queryMembers(int group_id);
    bool groupExist(int group_id);
    std::string getGroupName(int group_id);
    bool isInGroup(int group_id, int user_id);
    bool applyGroup(int group_id, int user_id, std::vector<int>& users);
    bool processGroupRequest(int group_id, int user_id, bool agree);
    int getRole(int group_id, int user_id);
    bool addMember(int group_id, int user_id, int role);
    bool delMember(int group_id, int user_id);
    bool dissolveGroup(int group_id);
    bool setRole(int group_id, int target_id, int role);
    bool removeFromAll(int user_id);

   private:
    MySQL m_mysql;
};