#include "EmailSender.h"

#include <curl/curl.h>
#include <glog/logging.h>

#include "../common/Config.h"
#include <cstring>
#include <sstream>
#include <thread>

std::once_flag EmailSender::s_init_flag;

struct UploadContext {
    const char* data;
    size_t size;
    size_t pos;
};

EmailSender::EmailSender() {
    std::call_once(s_init_flag,
                   []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
    m_sender = envOr("CHATROOM_SMTP_USER", "");
    m_auth_code = envOr("CHATROOM_SMTP_AUTH", "");
    m_smtp_url = envOr("CHATROOM_SMTP_URL", "smtps://smtp.qq.com:465");
    m_dry_run = envBool("CHATROOM_SMTP_DRY_RUN", false);
}

bool EmailSender::sendCode(const std::string& to, const std::string& code) {
    if (m_dry_run) {
        // 仅用于自动化测试：不发信，把验证码打到日志由测试脚本读取。
        // 生产环境必须确保 CHATROOM_SMTP_DRY_RUN 未设置。
        LOG(WARNING) << "[DRY-RUN] skip smtp, code for " << to << " = " << code;
        return true;
    }
    if (m_sender.empty() || m_auth_code.empty()) {
        LOG(ERROR) << "SMTP 未配置: 请设置 CHATROOM_SMTP_USER / "
                      "CHATROOM_SMTP_AUTH";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    struct curl_slist* recipients = nullptr;
    recipients = curl_slist_append(recipients, to.c_str());
    std::string mail_text = "From: " + m_sender + "\r\nTo: " + to +
                            "\r\nSubject: ChatRoom 验证码\r\n\r\n" + code +
                            "\r\n";
    UploadContext ctx = {mail_text.data(), mail_text.size(), 0};
    curl_easy_setopt(curl, CURLOPT_URL, m_smtp_url.c_str());
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_USERNAME, m_sender.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, m_auth_code.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, m_sender.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(
        curl, CURLOPT_READFUNCTION,
        +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
            auto* ctx = static_cast<UploadContext*>(userdata);
            size_t remaining = ctx->size - ctx->pos;
            size_t to_copy = size * nmemb;
            if (to_copy > remaining) {
                to_copy = remaining;
            }
            if (to_copy == 0) {
                return 0;
            }
            memcpy(ptr, ctx->data + ctx->pos, to_copy);
            ctx->pos += to_copy;
            return to_copy;
        });
    curl_easy_setopt(curl, CURLOPT_READDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_MAIL_AUTH, m_sender.c_str());
    curl_easy_setopt(curl, CURLOPT_LOGIN_OPTIONS, "AUTH=LOGIN");

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}