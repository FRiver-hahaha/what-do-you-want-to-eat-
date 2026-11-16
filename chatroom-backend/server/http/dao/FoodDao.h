#pragma once

#include <string>
#include <vector>

class MySQLPool;

// foods 一行：字段与 Agent 侧 MOCK_FOODS 对齐。
// meal_types 保持 JSON 文本形态，由 Service 层决定对外数组结构。
struct FoodRow {
    std::string id;
    std::string name;
    std::string category;
    std::string description;
    double calories = 0;
    double protein = 0;
    double fiber = 0;
    double veggies = 0;
    double water = 0;
    std::string nature = "recommend";
    std::string keywords;
    std::string image;
    std::string mealTypes = "[]";
    int price = 0;
    bool available = true;
};

class FoodDao {
   public:
    enum class Lookup { Found, Unavailable };

    explicit FoodDao(MySQLPool& pool) : m_pool(pool) {}

    // 只取 available=1 的菜品；餐次/标签/关键词/忌口过滤在 Service 层
    // 按 Agent 的 mock 语义执行，保证真实模式与 mock 结果一致。
    Lookup listAvailable(std::vector<FoodRow>& out) const;

   private:
    MySQLPool& m_pool;
};