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

// 中文：仅比较已计算出的算法值，不比较路径、大小或修改时间；顺序不同也能正确匹配 / English: Compare calculated algorithm values only, ignoring path, size, and modified time; matching is order-independent
bool hash_results_equal(const HashFileResult& left, const HashFileResult& right) noexcept;

using CancelCheck = std::function<bool()>;
using ProgressCallback = std::function<void(std::uint64_t bytes_read,
                                            std::uint64_t total_bytes)>;
using BatchProgressCallback = std::function<void(std::size_t file_index,
                                                 std::uint64_t bytes_read,
                                                 std::uint64_t total_bytes)>;

}  // 命名空间 filehash / Namespace filehash
