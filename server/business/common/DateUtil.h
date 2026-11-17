#pragma once

#include <cstddef>
#include <cstdio>
#include <ctime>
#include <string>

namespace eat::business::dateutil {

inline std::tm LocalNow() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    return tm;
}

inline std::string FormatDate(const std::tm& tm) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday);
    return std::string(buf);
}

inline std::string FormatClock(const std::tm& tm) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    return std::string(buf);
}

inline std::string Today() { return FormatDate(LocalNow()); }

inline std::string ClockNow() { return FormatClock(LocalNow()); }

inline bool Parse(const std::string& text, std::tm& out) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (text[i] < '0' || text[i] > '9') return false;
    }
    std::tm tm{};
    tm.tm_year = std::stoi(text.substr(0, 4)) - 1900;
    tm.tm_mon = std::stoi(text.substr(5, 2)) - 1;
    tm.tm_mday = std::stoi(text.substr(8, 2));
    tm.tm_hour = 12;
    if (tm.tm_mon < 0 || tm.tm_mon > 11 || tm.tm_mday < 1 || tm.tm_mday > 31) {
        return false;
    }
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

inline bool ValidDate(const std::string& text) {
    if (text.size() != 10) return false;
    std::tm tm{};
    if (!Parse(text, tm)) return false;
    const int year = tm.tm_year + 1900;
    return year >= 2000 && year <= 2100;
}

inline bool ValidClock(const std::string& text) {
    if (text.size() != 5 || text[2] != ':') return false;
    if (text[0] < '0' || text[0] > '2' || text[1] < '0' || text[1] > '9' ||
        text[3] < '0' || text[3] > '5' || text[4] < '0' || text[4] > '9') {
        return false;
    }
    const int hour = std::stoi(text.substr(0, 2));
    const int minute = std::stoi(text.substr(3, 2));
    return hour <= 23 && minute <= 59;
}

inline std::string AddDays(const std::string& date, int delta) {
    std::tm tm{};
    if (!Parse(date, tm)) return date;
    const std::time_t epoch = timegm(&tm) + static_cast<std::time_t>(delta) * 86400;
    std::tm shifted{};
    gmtime_r(&epoch, &shifted);
    return FormatDate(shifted);
}

inline std::string WeekStart(const std::string& date) {
    std::tm tm{};
    if (!Parse(date, tm)) return date;
    const int weekday = tm.tm_wday;
    const int back = weekday == 0 ? 6 : weekday - 1;
    return AddDays(date, -back);
}

}  // namespace eat::business::dateutil
