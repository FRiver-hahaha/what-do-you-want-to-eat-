#pragma once

#include "http/Router.h"
#include "http/service/StatsService.h"
#include "httplib.h"

class StatsController {
   public:
    explicit StatsController(StatsService& service) : m_service(service) {}

    void nutritionGap(const httplib::Request& req, httplib::Response& res,
                      const Router::Captures& captures);
    void weeklyStats(const httplib::Request& req, httplib::Response& res,
                     const Router::Captures& captures);

   private:
    StatsService& m_service;
};