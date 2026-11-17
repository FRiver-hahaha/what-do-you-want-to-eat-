#pragma once

#include "http/Router.h"
#include "http/service/FriendService.h"
#include "httplib.h"

class FriendController {
   public:
    explicit FriendController(FriendService& service) : m_service(service) {}

    void friends(const httplib::Request& req, httplib::Response& res,
                 const Router::Captures& captures);

   private:
    FriendService& m_service;
};