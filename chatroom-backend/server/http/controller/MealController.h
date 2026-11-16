#pragma once

#include "http/Router.h"
#include "http/service/MealService.h"
#include "httplib.h"

class MealController {
   public:
    explicit MealController(MealService& service) : m_service(service) {}

    void record(const httplib::Request& req, httplib::Response& res,
                const Router::Captures& captures);
    void recent(const httplib::Request& req, httplib::Response& res,
                const Router::Captures& captures);

   private:
    MealService& m_service;
};