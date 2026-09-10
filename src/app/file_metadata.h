#pragma once

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

namespace filehash::ui {

inline std::string format_file_time(const std::filesystem::path& path) {
    std::error_code error;
    const auto file_time = std::filesystem::last_write_time(path, error);
    if (error) return "Unavailable";

    const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        file_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(system_time);
    std::tm local_time{};
#ifdef _WIN32
    if (localtime_s(&local_time, &timestamp) != 0) return "Unavailable";
#else
    if (localtime_r(&timestamp, &local_time) == nullptr) return "Unavailable";
#endif
    std::ostringstream output;
    output << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

}  // 命名空间 filehash::ui / Namespace filehash::ui
