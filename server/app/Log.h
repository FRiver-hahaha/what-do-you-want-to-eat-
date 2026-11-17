#pragma once

#include <string>

namespace eat::app {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

class Log {
 public:
    static void SetLevel(LogLevel level);
    static void Write(LogLevel level, const std::string& module,
                      const std::string& message);
    static std::string Redact(const std::string& text);
};

}  // namespace eat::app