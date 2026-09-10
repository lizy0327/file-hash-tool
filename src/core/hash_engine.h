#pragma once

#include "core/hash_types.h"

#include <cstddef>
#include <vector>

namespace filehash {

HashFileResult hash_file(const std::filesystem::path& path,
                         const std::vector<HashAlgorithm>& algorithms,
                         const CancelCheck& cancel = {},
                         const ProgressCallback& progress = {},
                         std::size_t buffer_size = 4u * 1024u * 1024u);

std::vector<HashFileResult> hash_files(const std::vector<std::filesystem::path>& paths,
                                       const std::vector<HashAlgorithm>& algorithms,
                                       std::size_t worker_count = 0,
                                       const CancelCheck& cancel = {},
                                       const BatchProgressCallback& progress = {});

std::string format_hex(const std::vector<std::uint8_t>& bytes);

}  // 命名空间 filehash / Namespace filehash
