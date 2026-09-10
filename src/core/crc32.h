#pragma once

#include <cstddef>
#include <cstdint>

namespace filehash {

class Crc32 {
public:
    Crc32() noexcept = default;

    void update(const std::uint8_t* data, std::size_t size) noexcept;
    std::uint32_t finish() const noexcept;

private:
    std::uint32_t value_ = 0xFFFFFFFFu;
};

}  // 命名空间 filehash / Namespace filehash
