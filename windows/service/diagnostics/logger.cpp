#include "logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>

namespace phonebridge {

void Logger::log(LogLevel level, const std::string& component, const std::string& message) {
    const char* levelStr = "INFO";
    if (level == LogLevel::Warn) levelStr = "WARN";
    if (level == LogLevel::Error) levelStr = "ERROR";

    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);

    std::cout << "[" << std::put_time(std::localtime(&timeT), "%Y-%m-%d %H:%M:%S") << "] "
              << "[" << levelStr << "] "
              << "[" << component << "] "
              << message << "\n";
}

} // namespace phonebridge
