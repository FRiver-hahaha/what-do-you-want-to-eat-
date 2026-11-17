#pragma once

#include <cstdlib>
#include <string>

// 配置读取：全部来自环境变量，源码内不保留任何真实凭据。
// 变量清单见仓库根目录 .env.example。
inline std::string envOr(const char* key, const std::string& def) {
    const char* v = std::getenv(key);
    return (v != nullptr && *v != '\0') ? std::string(v) : def;
}

inline int envInt(const char* key, int def) {
    const char* v = std::getenv(key);
    if (v == nullptr || *v == '\0') {
        return def;
    }
    try {
        return std::stoi(v);
    } catch (...) {
        return def;
    }
}

inline bool envBool(const char* key, bool def = false) {
    const char* v = std::getenv(key);
    if (v == nullptr || *v == '\0') {
        return def;
    }
    std::string s(v);
    return s == "1" || s == "true" || s == "TRUE" || s == "yes";
}
