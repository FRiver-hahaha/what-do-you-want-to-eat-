#pragma once

#include <string>
#include <vector>

#include "server/database/MySQLPool.h"

namespace eat::business {

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
    std::string meal_types = "[]";
    int price = 0;
    bool available = true;
};

class FoodDao {
 public:
    enum class Lookup { Found, Unavailable };

    explicit FoodDao(db::MySQLPool& pool) : pool_(pool) {}

    Lookup ListAvailable(std::vector<FoodRow>& out) const;

 private:
    db::MySQLPool& pool_;
};

}  // namespace eat::business
