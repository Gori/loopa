#include "Logger.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <string>
#include <thread>

namespace loopa {

namespace fs = std::filesystem;

Logger& Logger::instance() {
    static Logger s;
    return s;
}

Logger::~Logger() {
    shutdown();
}

void Logger::init(Config cfg) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_initialized) {
        return;
    }
    m_cfg = std::move(cfg);
    std::error_code ec;
    fs::create_directories(fs::path(m_cfg.logDir), ec);
    openFile();
    m_initialized = true;
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_file != nullptr) {
        std::fflush(m_file);
        std::fclose(m_file);
        m_file = nullptr;
    }
    m_initialized = false;
}

void Logger::openFile() {
    const fs::path path = fs::path(m_cfg.logDir) / m_cfg.fileBaseName;
    m_file = std::fopen(path.string().c_str(), "ab");
    if (m_file != nullptr) {
        std::fseek(m_file, 0, SEEK_END);
        const long pos = std::ftell(m_file);
        m_fileBytes = (pos > 0) ? static_cast<std::size_t>(pos) : 0;
    } else {
        m_fileBytes = 0;
    }
}

void Logger::rotateIfNeeded(std::size_t nextWriteBytes) {
    if (m_file == nullptr) {
        return;
    }
    if (m_fileBytes + nextWriteBytes <= m_cfg.maxFileBytes) {
        return;
    }
    std::fflush(m_file);
    std::fclose(m_file);
    m_file = nullptr;

    const fs::path dir = fs::path(m_cfg.logDir);
    const fs::path base = dir / m_cfg.fileBaseName;
    std::error_code ec;

    for (int i = m_cfg.maxRotatedFiles; i >= 1; --i) {
        const fs::path src = (i == 1) ? base : fs::path(base.string() + "." + std::to_string(i - 1));
        const fs::path dst = fs::path(base.string() + "." + std::to_string(i));
        if (fs::exists(src, ec)) {
            fs::remove(dst, ec);
            fs::rename(src, dst, ec);
        }
    }
    openFile();
}

const char* Logger::levelTag(LogLevel l) {
    switch (l) {
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

std::string Logger::formatTimestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t = system_clock::to_time_t(now);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    const int n = std::snprintf(buf, sizeof(buf),
        "%04d-%02d-%02d %02d:%02d:%02d.%03d",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec,
        static_cast<int>(ms.count()));
    return std::string(buf, static_cast<std::size_t>(n));
}

std::string Logger::threadTag() {
    std::ostringstream os;
    os << std::this_thread::get_id();
    return os.str();
}

void Logger::writeLine(LogLevel level, std::string_view msg) {
    std::string line;
    line.reserve(msg.size() + 64);
    line += formatTimestamp();
    line += " [";
    line += levelTag(level);
    line += "] [";
    line += threadTag();
    line += "] ";
    line.append(msg.data(), msg.size());
    line += '\n';

    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);

    rotateIfNeeded(line.size());
    if (m_file != nullptr) {
        std::fwrite(line.data(), 1, line.size(), m_file);
        std::fflush(m_file);
        m_fileBytes += line.size();
    }
}

void Logger::log(LogLevel level, std::string_view msg) {
    std::lock_guard<std::mutex> lk(m_mutex);
    writeLine(level, msg);
}

}  // namespace loopa
