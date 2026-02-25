#ifndef MCP_MQTT_LOGGER_H
#define MCP_MQTT_LOGGER_H

#include <iostream>
#include <sstream>
#include <string>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <mutex>

namespace mcp_mqtt {

/**
 * @brief Log levels for the MCP SDK logger.
 *
 * Levels are ordered by severity. Setting the log level to a given value
 * will enable all messages at that level and above.
 */
enum class LogLevel {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3,
    OFF   = 4
};

/**
 * @brief Simple, thread-safe, header-only logger for MCP SDK.
 *
 * Usage:
 *   // Set log level at runtime (default is INFO)
 *   Logger::setLevel(LogLevel::DEBUG);
 *
 *   // Use macros for convenient logging
 *   MCP_LOG_DEBUG("Received message on topic: " << topic);
 *   MCP_LOG_INFO("Server started with id: " << serverId);
 *   MCP_LOG_WARN("Client session not found: " << clientId);
 *   MCP_LOG_ERROR("Failed to parse JSON-RPC message");
 */
class Logger {
public:
    static void setLevel(LogLevel level) {
        levelRef().store(level, std::memory_order_relaxed);
    }

    static LogLevel getLevel() {
        return levelRef().load(std::memory_order_relaxed);
    }

    static bool isEnabled(LogLevel level) {
        return level >= levelRef().load(std::memory_order_relaxed);
    }

    static void log(LogLevel level, const std::string& message) {
        if (!isEnabled(level)) return;

        std::lock_guard<std::mutex> lock(mutexRef());
        std::ostream& out = (level >= LogLevel::WARN) ? std::cerr : std::cout;
        out << timestamp() << " [" << levelStr(level) << "] [mcp] " << message << std::endl;
    }

private:
    static std::atomic<LogLevel>& levelRef() {
        static std::atomic<LogLevel> instance{LogLevel::INFO};
        return instance;
    }

    static std::mutex& mutexRef() {
        static std::mutex instance;
        return instance;
    }

    static const char* levelStr(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERROR: return "ERROR";
            default:              return "?????";
        }
    }

    static std::string timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &time);
#else
        localtime_r(&time, &tm);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }
};

// Convenience macros - the isEnabled() check avoids string construction overhead
#define MCP_LOG_DEBUG(msg) \
    do { \
        if (::mcp_mqtt::Logger::isEnabled(::mcp_mqtt::LogLevel::DEBUG)) { \
            std::ostringstream _mcp_oss; \
            _mcp_oss << msg; \
            ::mcp_mqtt::Logger::log(::mcp_mqtt::LogLevel::DEBUG, _mcp_oss.str()); \
        } \
    } while (0)

#define MCP_LOG_INFO(msg) \
    do { \
        if (::mcp_mqtt::Logger::isEnabled(::mcp_mqtt::LogLevel::INFO)) { \
            std::ostringstream _mcp_oss; \
            _mcp_oss << msg; \
            ::mcp_mqtt::Logger::log(::mcp_mqtt::LogLevel::INFO, _mcp_oss.str()); \
        } \
    } while (0)

#define MCP_LOG_WARN(msg) \
    do { \
        if (::mcp_mqtt::Logger::isEnabled(::mcp_mqtt::LogLevel::WARN)) { \
            std::ostringstream _mcp_oss; \
            _mcp_oss << msg; \
            ::mcp_mqtt::Logger::log(::mcp_mqtt::LogLevel::WARN, _mcp_oss.str()); \
        } \
    } while (0)

#define MCP_LOG_ERROR(msg) \
    do { \
        if (::mcp_mqtt::Logger::isEnabled(::mcp_mqtt::LogLevel::ERROR)) { \
            std::ostringstream _mcp_oss; \
            _mcp_oss << msg; \
            ::mcp_mqtt::Logger::log(::mcp_mqtt::LogLevel::ERROR, _mcp_oss.str()); \
        } \
    } while (0)

} // namespace mcp_mqtt

#endif // MCP_MQTT_LOGGER_H
