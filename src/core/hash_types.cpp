#include "core/hash_types.h"

#include <algorithm>

namespace filehash {

const char* algorithm_name(const HashAlgorithm algorithm) noexcept {
    switch (algorithm) {
        case HashAlgorithm::Crc32: return "CRC32";
        case HashAlgorithm::Md5: return "MD5";
        case HashAlgorithm::Sha1: return "SHA-1";
        case HashAlgorithm::Sha256: return "SHA-256";
        case HashAlgorithm::Sha384: return "SHA-384";
        case HashAlgorithm::Sha512: return "SHA-512";
    }
    return "Unknown";
}

std::size_t digest_size(const HashAlgorithm algorithm) noexcept {
    switch (algorithm) {
        case HashAlgorithm::Crc32: return 4;
        case HashAlgorithm::Md5: return 16;
        case HashAlgorithm::Sha1: return 20;
        case HashAlgorithm::Sha256: return 32;
        case HashAlgorithm::Sha384: return 48;
        case HashAlgorithm::Sha512: return 64;
    }
    return 0;
}

bool hash_results_equal(const HashFileResult& left, const HashFileResult& right) noexcept {
    if (!left.error.empty() || !right.error.empty() || left.cancelled || right.cancelled ||
        left.values.empty() || right.values.empty() || left.values.size() != right.values.size()) {
        return false;
    }
    for (const auto& left_value : left.values) {
        const auto right_value = std::find_if(right.values.begin(), right.values.end(),
                                              [&left_value](const HashValue& value) {
                                                  return value.algorithm == left_value.algorithm;
                                              });
        if (right_value == right.values.end() || right_value->bytes != left_value.bytes) return false;
    }
    return true;
}

}  // 命名空间 filehash / Namespace filehash
