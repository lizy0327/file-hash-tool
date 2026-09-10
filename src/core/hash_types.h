#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace filehash {

enum class HashAlgorithm {
    Crc32,
    Md5,
    Sha1,
    Sha256,
    Sha384,
    Sha512,
};

const char* algorithm_name(HashAlgorithm algorithm) noexcept;
std::size_t digest_size(HashAlgorithm algorithm) noexcept;

struct HashValue {
    HashAlgorithm algorithm;
    std::vector<std::uint8_t> bytes;
};

struct HashFileResult {
    std::filesystem::path path;
    std::uint64_t bytes_read = 0;
    std::vector<HashValue> values;
    std::string error;
    bool cancelled = false;
};

using CancelCheck = std::function<bool()>;
using ProgressCallback = std::function<void(std::uint64_t bytes_read,
                                            std::uint64_t total_bytes)>;
using BatchProgressCallback = std::function<void(std::size_t file_index,
                                                 std::uint64_t bytes_read,
                                                 std::uint64_t total_bytes)>;

}  // 命名空间 filehash / Namespace filehash
