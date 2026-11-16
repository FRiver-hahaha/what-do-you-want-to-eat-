#pragma once

#include <string>
#include <vector>

#include "http/service/ServiceResult.h"

class MySQLPool;

// 菜单与菜品搜索：过滤语义逐条对齐 Agent 的 ServiceClient mock 分支，
// 保证真实模式与 mock 在同样输入下给出同样的菜品集合。
class FoodService {
   public:
    // defaultUserId：Agent 的 /api/v1/menu 与 /api/v1/foods/search 不携带
    // user_id（契约缺口），未显式指定时用该用户加载过敏/忌口。
    FoodService(MySQLPool& pool, std::string defaultUserId);

    ServiceResult menu(const std::string& userId,
                       const std::string& mealType) const;

    ServiceResult search(const std::string& userId, const std::string& query,
                         const std::vector<std::string>& tags,
                         const std::string& mealType, int limit,
                         const std::string& nature) const;

    const std::string& defaultUserId() const { return m_defaultUserId; }

   private:
    MySQLPool& m_pool;
    std::string m_defaultUserId;
};