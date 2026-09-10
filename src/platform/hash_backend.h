#pragma once

#include "core/hash_types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace filehash {

class HashContext {
public:
    virtual ~HashContext() = default;
    virtual void update(const std::uint8_t* data, std::size_t size) = 0;
    virtual std::vector<std::uint8_t> finish() = 0;
};

std::unique_ptr<HashContext> create_hash_context(HashAlgorithm algorithm);

}  // 命名空间 filehash / Namespace filehash
