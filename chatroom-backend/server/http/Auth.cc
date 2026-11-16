#include "http/Auth.h"

#include <cstddef>

namespace {

// 常量时间比较，避免按字节提前返回泄露 Token 前缀。
bool secureEquals(const std::string& a, const std::string& b) {
    unsigned char diff = (a.size() == b.size()) ? 0 : 1;
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

}  // namespace

bool Auth::init(const std::string& token, const std::string& userMode) {
    m_token = token;
    m_mode = userMode.empty() ? "p4_demo" : userMode;
    if (m_token.empty()) {
        return false;
    }
    return m_mode == "p4_demo";
}

bool Auth::checkToken(const httplib::Request& req) const {
    if (m_token.empty()) {
        return false;
    }
    auto it = req.headers.find("Authorization");
    if (it == req.headers.end()) {
        return false;
    }
    static const std::string kPrefix = "Bearer ";
    const std::string& value = it->second;
    if (value.size() <= kPrefix.size() ||
        value.compare(0, kPrefix.size(), kPrefix) != 0) {
        return false;
    }
    return secureEquals(value.substr(kPrefix.size()), m_token);
}

bool Auth::allowUid(const std::string& uid) const {
    if (m_mode != "p4_demo") {
        return false;
    }
    return uid == "u_demo";
}

bool Auth::validUid(const std::string& uid) {
    if (uid.empty() || uid.size() > 64) {
        return false;
    }
    for (const char c : uid) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}