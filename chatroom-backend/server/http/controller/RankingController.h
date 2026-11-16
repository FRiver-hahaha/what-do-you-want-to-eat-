#pragma once

#include "http/Router.h"
#include "http/service/RankingService.h"
#include "httplib.h"

class RankingController {
   public:
    explicit RankingController(RankingService& service) : m_service(service) {}

    void friendRanking(const httplib::Request& req, httplib::Response& res,
                       const Router::Captures& captures);

   private:
    RankingService& m_service;
};