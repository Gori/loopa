#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>

namespace loopa {

enum class LogLevel : std::uint8_t { Info, Warn, Error };

class Logger {
public:
    struct Config {
        std::string logDir;
        std::string fileBaseName = "loopa.log";
        std::size_t maxFileBytes = 10ull * 1024ull * 1024ull;
        int maxRotatedFiles = 3;
    };

    static Logger& instance();

    void init(Config cfg);
    void shutdown();

    void log(LogLevel level, std::string_view msg);

    void info(std::string_view msg)  { log(LogLevel::Info,  msg); }
    void warn(std::string_view msg)  { log(LogLevel::Warn,  msg); }
    void error(std::string_view msg) { log(LogLevel::Error, msg); }

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void writeLine(LogLevel level, std::string_view msg);
    void rotateIfNeeded(std::size_t nextWriteBytes);
    void openFile();
    static const char* levelTag(LogLevel l);
    static std::string formatTimestamp();
    static std::string threadTag();

    std::mutex m_mutex;
    Config m_cfg{};
    std::FILE* m_file = nullptr;
    std::size_t m_fileBytes = 0;
    bool m_initialized = false;
};

}  // namespace loopa

#define LOG_INFO(msg)  ::loopa::Logger::instance().info(msg)
#define LOG_WARN(msg)  ::loopa::Logger::instance().warn(msg)
#define LOG_ERROR(msg) ::loopa::Logger::instance().error(msg)
