#pragma once

#include "http/Router.h"
#include "httplib.h"
#include "http/service/ProfileService.h"

class ProfileController {
   public:
    explicit ProfileController(ProfileService& service) : m_service(service) {}

    void get(const httplib::Request& req, httplib::Response& res,
             const Router::Captures& captures);
    void patch(const httplib::Request& req, httplib::Response& res,
               const Router::Captures& captures);

   private:
    ProfileService& m_service;
};