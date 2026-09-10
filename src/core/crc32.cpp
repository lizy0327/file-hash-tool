#include "core/crc32.h"

namespace filehash {

void Crc32::update(const std::uint8_t* data, const std::size_t size) noexcept {
    for (std::size_t i = 0; i < size; ++i) {
        value_ ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const auto mask = 0u - (value_ & 1u);
            value_ = (value_ >> 1u) ^ (0xEDB88320u & mask);
        }
    }
}

std::uint32_t Crc32::finish() const noexcept {
    return value_ ^ 0xFFFFFFFFu;
}

}  // 命名空间 filehash / Namespace filehash
