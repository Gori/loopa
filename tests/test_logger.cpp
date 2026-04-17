#include <catch2/catch_test_macros.hpp>

#include "core/Logger.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

fs::path makeTempLogDir() {
    auto dir = fs::temp_directory_path() / "loopa-test-logger";
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

}  // namespace

TEST_CASE("Logger writes a line to the configured file", "[logger]") {
    const auto dir = makeTempLogDir();

    loopa::Logger::Config cfg{};
    cfg.logDir = dir.string();
    cfg.fileBaseName = "t.log";
    cfg.maxFileBytes = 1024 * 1024;
    cfg.maxRotatedFiles = 3;

    loopa::Logger::instance().init(cfg);
    loopa::Logger::instance().info("hello-logger");
    loopa::Logger::instance().shutdown();

    const auto contents = slurp(dir / "t.log");
    REQUIRE(contents.find("hello-logger") != std::string::npos);
    REQUIRE(contents.find("[INFO ]") != std::string::npos);
}

TEST_CASE("Logger rotates when the file exceeds max bytes", "[logger]") {
    const auto dir = makeTempLogDir();

    loopa::Logger::Config cfg{};
    cfg.logDir = dir.string();
    cfg.fileBaseName = "rot.log";
    cfg.maxFileBytes = 256;
    cfg.maxRotatedFiles = 2;

    loopa::Logger::instance().init(cfg);
    const std::string pad(180, 'x');
    loopa::Logger::instance().info(pad);
    loopa::Logger::instance().info(pad);
    loopa::Logger::instance().info(pad);
    loopa::Logger::instance().shutdown();

    REQUIRE(fs::exists(dir / "rot.log"));
    REQUIRE(fs::exists(dir / "rot.log.1"));
}
