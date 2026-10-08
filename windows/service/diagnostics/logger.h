#pragma once
#include <string>

namespace phonebridge {

enum class LogLevel {
    Info,
    Warn,
    Error
};

class Logger {
public:
    static void log(LogLevel level, const std::string& component, const std::string& message);
};

} // namespace phonebridge
