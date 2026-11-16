#pragma once

#include <ctime>
#include <cstdio>
#include <string>

// 业务日期一律使用进程本地时区（Agent 侧 date.today() 同为本地时区）。
// 日期运算走 UTC epoch，避免夏令时/时区把「加减一天」算成 23/25 小时。
namespace httpdate {

inline std::tm localNow() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    return tm;
}

inline std::string formatDate(const std::tm& tm) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday);
    return std::string(buf);
}

inline std::string formatClock(const std::tm& tm) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    return std::string(buf);
}

inline std::string today() { return formatDate(localNow()); }

inline std::string clockNow() { return formatClock(localNow()); }

inline bool parse(const std::string& text, std::tm& out) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == 4 || i == 7) {
            continue;
        }
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
    }
    std::tm tm{};
    tm.tm_year = std::stoi(text.substr(0, 4)) - 1900;
    tm.tm_mon = std::stoi(text.substr(5, 2)) - 1;
    tm.tm_mday = std::stoi(text.substr(8, 2));
    tm.tm_hour = 12;
    if (tm.tm_mon < 0 || tm.tm_mon > 11 || tm.tm_mday < 1 || tm.tm_mday > 31) {
        return false;
    }
    // timegm 可能规范化入参（2 月 30 日 → 3 月 2 日），比较前先留原始值。
    const int year = tm.tm_year;
    const int month = tm.tm_mon;
    const int mday = tm.tm_mday;
    const std::time_t epoch = timegm(&tm);
    std::tm normalized{};
    gmtime_r(&epoch, &normalized);
    if (normalized.tm_year != year || normalized.tm_mon != month ||
        normalized.tm_mday != mday) {
        return false;
    }
    out = normalized;
    return true;
}

inline bool validDate(const std::string& text) {
    std::tm tm{};
    if (!parse(text, tm)) {
        return false;
    }
    const int year = tm.tm_year + 1900;
    return year >= 2000 && year <= 2100;
}

inline std::string addDays(const std::string& date, int delta) {
    std::tm tm{};
    if (!parse(date, tm)) {
        return date;
    }
    const std::time_t epoch =
        timegm(&tm) + static_cast<std::time_t>(delta) * 86400;
    std::tm shifted{};
    gmtime_r(&epoch, &shifted);
    return formatDate(shifted);
}

// 以周一为一周起点，与 Agent 侧 get_weekly_stats 的 week 字段一致。
inline std::string weekStart(const std::string& date) {
    std::tm tm{};
    if (!parse(date, tm)) {
        return date;
    }
    const int weekday = tm.tm_wday;  // 0 = Sunday
    const int back = (weekday == 0) ? 6 : weekday - 1;
    return addDays(date, -back);
}

}  // namespace httpdate