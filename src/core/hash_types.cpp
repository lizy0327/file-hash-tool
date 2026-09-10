#include "core/hash_types.h"

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

}  // 命名空间 filehash / Namespace filehash
