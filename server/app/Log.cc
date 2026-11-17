#include "server/app/Log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>

namespace eat::app {
namespace {

std::atomic<int> g_level{static_cast<int>(LogLevel::Info)};
std::mutex g_mutex;

std::string Escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char c : value) {
        switch (c) {
            case '"': out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[7];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
                    out.append(buffer);
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

const char* LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warn: return "warn";
        case LogLevel::Error: return "error";
    }
    return "info";
}

}  // namespace

void Log::SetLevel(LogLevel level) {
    g_level.store(static_cast<int>(level));
}

void Log::Write(LogLevel level, const std::string& module, const std::string& message) {
    if (static_cast<int>(level) < g_level.load()) {
        return;
    }
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch())
                            .count();
    std::lock_guard<std::mutex> lock(g_mutex);
    std::printf("{\"ts\":%lld,\"level\":\"%s\",\"module\":\"%s\",\"message\":\"%s\"}\n",
                static_cast<long long>(millis), LevelName(level), Escape(module).c_str(),
                Escape(Redact(message)).c_str());
    std::fflush(stdout);
}

std::string Log::Redact(const std::string& text) {
    std::string out = text;
    const auto redact_after = [&](const std::string& marker) {
        std::size_t pos = 0;
        while ((pos = out.find(marker, pos)) != std::string::npos) {
            const std::size_t start = pos + marker.size();
            std::size_t end = start;
            while (end < out.size() && out[end] != '"' && out[end] != ' ' &&
                   out[end] != '\n' && out[end] != ',' && out[end] != '}') {
                ++end;
            }
            if (end > start) {
                out.replace(start, end - start, "[redacted]");
                pos = start + 10;
            } else {
                pos = start;
            }
        }
    };
    redact_after("Bearer ");
    redact_after("sk-");
    redact_after("\"password\":\"");
    redact_after("\"access_token\":\"");
    redact_after("\"refresh_token\":\"");
    return out;
}

}  // namespace eat::app