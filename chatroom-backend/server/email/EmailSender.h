#pragma once

#include <mutex>
#include <string>

class EmailSender {
   public:
    EmailSender();
    bool sendCode(const std::string& to, const std::string& code);

   private:
    static std::once_flag s_init_flag;
    // 改为实例成员：由环境变量注入，源码不再硬编码发件账号与授权码
    std::string m_sender;
    std::string m_auth_code;
    std::string m_smtp_url;
    bool m_dry_run{false};
};