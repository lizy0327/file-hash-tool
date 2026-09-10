#include "core/hash_engine.h"

#include "core/crc32.h"
#include "platform/hash_backend.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace filehash {
namespace {

struct ActiveHash {
    HashAlgorithm algorithm;
    Crc32 crc;
    std::unique_ptr<HashContext> context;
};

bool is_crc32(const HashAlgorithm algorithm) noexcept {
    return algorithm == HashAlgorithm::Crc32;
}

#ifdef _WIN32
struct HandleCloser {
    void operator()(void* handle) const noexcept {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) CloseHandle(static_cast<HANDLE>(handle));
    }
};
using ScopedHandle = std::unique_ptr<void, HandleCloser>;
#endif

}  // 命名空间 / Namespace

HashFileResult hash_file(const std::filesystem::path& path,
                         const std::vector<HashAlgorithm>& algorithms,
                         const CancelCheck& cancel,
                         const ProgressCallback& progress,
                         const std::size_t buffer_size) {
    HashFileResult result;
    result.path = path;

    if (algorithms.empty()) {
        result.error = "no algorithms selected";
        return result;
    }

    try {
#ifdef _WIN32
        ScopedHandle input(CreateFileW(path.c_str(), GENERIC_READ,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (!input || input.get() == INVALID_HANDLE_VALUE) {
            result.error = "unable to open file";
            return result;
        }
        LARGE_INTEGER native_size{};
        if (!GetFileSizeEx(static_cast<HANDLE>(input.get()), &native_size) || native_size.QuadPart < 0) {
            result.error = "unable to read file size";
            return result;
        }
        const auto total_size = static_cast<std::uint64_t>(native_size.QuadPart);
#else
        const auto total_size = std::filesystem::file_size(path);
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            result.error = "unable to open file";
            return result;
        }
#endif

        std::vector<ActiveHash> active;
        active.reserve(algorithms.size());
        for (const auto algorithm : algorithms) {
            ActiveHash item{algorithm, {}, nullptr};
            if (!is_crc32(algorithm)) item.context = create_hash_context(algorithm);
            active.push_back(std::move(item));
        }

        std::vector<std::uint8_t> buffer(std::max<std::size_t>(buffer_size, 64u * 1024u));
        auto consume = [&](const std::size_t bytes) {
            for (auto& item : active) {
                if (is_crc32(item.algorithm)) {
                    item.crc.update(buffer.data(), bytes);
                } else {
                    item.context->update(buffer.data(), bytes);
                }
            }
            result.bytes_read += bytes;
            if (progress) progress(result.bytes_read, total_size);
        };

#ifdef _WIN32
        while (true) {
            if (cancel && cancel()) {
                result.cancelled = true;
                return result;
            }
            DWORD count = 0;
            if (!ReadFile(static_cast<HANDLE>(input.get()), buffer.data(),
                          static_cast<DWORD>(buffer.size()), &count, nullptr)) {
                result.error = "read error";
                return result;
            }
            if (count == 0) break;
            consume(static_cast<std::size_t>(count));
        }
#else
        while (input) {
            if (cancel && cancel()) {
                result.cancelled = true;
                return result;
            }
            input.read(reinterpret_cast<char*>(buffer.data()),
                       static_cast<std::streamsize>(buffer.size()));
            const auto count = input.gcount();
            if (count <= 0) break;
            consume(static_cast<std::size_t>(count));
        }
        if (input.bad()) {
            result.error = "read error";
            return result;
        }
#endif

        for (auto& item : active) {
            HashValue value{item.algorithm, {}};
            if (is_crc32(item.algorithm)) {
                const auto crc = item.crc.finish();
                value.bytes = {
                    static_cast<std::uint8_t>((crc >> 24u) & 0xFFu),
                    static_cast<std::uint8_t>((crc >> 16u) & 0xFFu),
                    static_cast<std::uint8_t>((crc >> 8u) & 0xFFu),
                    static_cast<std::uint8_t>(crc & 0xFFu),
                };
            } else {
                value.bytes = item.context->finish();
            }
            result.values.push_back(std::move(value));
        }
    } catch (const std::exception& error) {
        result.error = error.what();
    }

    return result;
}

std::vector<HashFileResult> hash_files(const std::vector<std::filesystem::path>& paths,
                                       const std::vector<HashAlgorithm>& algorithms,
                                       std::size_t worker_count,
                                       const CancelCheck& cancel,
                                       const BatchProgressCallback& progress) {
    std::vector<HashFileResult> results(paths.size());
    if (paths.empty()) return results;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        results[index].path = paths[index];
    }

    if (worker_count == 0) {
        worker_count = std::max<std::size_t>(1, std::thread::hardware_concurrency());
    }
    worker_count = std::min(worker_count, paths.size());

    std::atomic<std::size_t> next{0};
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&] {
            while (true) {
                if (cancel && cancel()) return;
                const auto index = next.fetch_add(1);
                if (index >= paths.size()) return;
                const ProgressCallback file_progress = [&progress, index](const std::uint64_t bytes_read,
                                                                          const std::uint64_t total_bytes) {
                    if (progress) progress(index, bytes_read, total_bytes);
                };
                results[index] = hash_file(paths[index], algorithms, cancel, file_progress);
            }
        });
    }
    for (auto& worker : workers) worker.join();
    if (cancel && cancel()) {
        for (auto& result : results) {
            if (result.values.empty() && result.error.empty() && !result.cancelled) {
                result.cancelled = true;
            }
        }
    }
    return results;
}

std::string format_hex(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : bytes) output << std::setw(2) << static_cast<unsigned int>(byte);
    return output.str();
}

}  // 命名空间 filehash / Namespace filehash
