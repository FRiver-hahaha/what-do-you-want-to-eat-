#pragma once

#include <string>

#include "httplib.h"

// 认证层：只做 Token 校验、UID 形状校验与演示模式白名单。
// 必须在任何 Controller / DAO 之前完成，禁止“先查库再判权”。
class Auth {
   public:
    // p4_demo 为唯一允许的用户模式；token 缺失或模式未知一律初始化失败（fail-closed）。
    bool init(const std::string& token, const std::string& userMode);

    bool checkToken(const httplib::Request& req) const;
    bool allowUid(const std::string& uid) const;
    static bool validUid(const std::string& uid);

    const std::string& mode() const { return m_mode; }

   private:
    std::string m_token;
    std::string m_mode;
};