#include "core/hash_engine.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: filehash_benchmark <file> [md5|all] [buffer_mib]\n";
        return 2;
    }

    std::vector<filehash::HashAlgorithm> algorithms;
    const std::string mode = argc >= 3 ? argv[2] : "md5";
    const std::size_t buffer_mib = argc >= 4 ? static_cast<std::size_t>(std::stoull(argv[3])) : 16u;
    if (mode == "all") {
        algorithms = {filehash::HashAlgorithm::Crc32, filehash::HashAlgorithm::Md5,
                      filehash::HashAlgorithm::Sha1, filehash::HashAlgorithm::Sha256,
                      filehash::HashAlgorithm::Sha384, filehash::HashAlgorithm::Sha512};
    } else {
        algorithms = {filehash::HashAlgorithm::Md5};
    }

    const auto started = std::chrono::steady_clock::now();
    const auto result = filehash::hash_file(argv[1], algorithms, {}, {}, buffer_mib * 1024u * 1024u);
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (!result.error.empty() || result.cancelled) {
        std::cerr << (result.error.empty() ? "cancelled" : result.error) << '\n';
        return 1;
    }

    const double mib_per_second = elapsed > 0.0
        ? static_cast<double>(result.bytes_read) / (1024.0 * 1024.0) / elapsed
        : 0.0;
    std::cout << "buffer_mib=" << buffer_mib << " bytes=" << result.bytes_read << " seconds=" << std::fixed << std::setprecision(3)
              << elapsed << " MiB/s=" << std::setprecision(1) << mib_per_second << '\n';
    return 0;
}
