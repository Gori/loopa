#pragma once

#include "core/Logger.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace loopa {

// Write a mono float32 buffer to a 32-bit-float WAV at `path`. Creates the
// parent directory if missing. Logs a WARN on open failure and returns
// silently — diagnostic helper, never blocks the caller. No external deps.
inline void writeWavMonoF32(const std::string& path, const float* data,
                             std::size_t n, double sr) {
    if (!data || n == 0) return;
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        LOG_WARN("writeWavMonoF32: failed to open " + path);
        return;
    }
    auto w32 = [&](int32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](int16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const int32_t nSamples = static_cast<int32_t>(n);
    const int32_t byteRate = static_cast<int32_t>(sr) * 4;
    f.write("RIFF", 4);   w32(36 + nSamples * 4);
    f.write("WAVE", 4);   f.write("fmt ", 4);   w32(16);
    w16(3); w16(1);                             // fmt=IEEE float, 1 ch
    w32(static_cast<int32_t>(sr));
    w32(byteRate); w16(4); w16(32);             // block align, bits per sample
    f.write("data", 4);   w32(nSamples * 4);
    f.write(reinterpret_cast<const char*>(data), nSamples * 4);
    LOG_INFO("writeWavMonoF32: wrote " + path + " (" + std::to_string(n)
             + " samples @ " + std::to_string(static_cast<int>(sr)) + " Hz)");
}

}  // namespace loopa
