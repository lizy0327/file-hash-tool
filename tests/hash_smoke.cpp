#include "app/theme.h"
#include "core/crc32.h"
#include "core/hash_engine.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <atomic>

int main() {
    using namespace filehash;

    static_assert(ui::kThemes.size() == 6);
    static_assert(ui::theme_from_index(99) == ui::ThemeId::ArcticBlue);
    assert(std::string(ui::theme_info(ui::ThemeId::GraphiteAmber).slug) == "graphite-amber");
    assert(ui::theme_info(ui::ThemeId::MidnightCyan).dark);

    Crc32 crc;
    const std::string vector = "123456789";
    crc.update(reinterpret_cast<const std::uint8_t*>(vector.data()), vector.size());
    assert(format_hex({
        static_cast<std::uint8_t>((crc.finish() >> 24u) & 0xFFu),
        static_cast<std::uint8_t>((crc.finish() >> 16u) & 0xFFu),
        static_cast<std::uint8_t>((crc.finish() >> 8u) & 0xFFu),
        static_cast<std::uint8_t>(crc.finish() & 0xFFu),
    }) == "cbf43926");

    const auto path = std::filesystem::temp_directory_path() / "filehash_smoke_123456789.txt";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << vector;
    }

    const auto result = hash_file(path, {
        HashAlgorithm::Crc32,
        HashAlgorithm::Md5,
        HashAlgorithm::Sha1,
        HashAlgorithm::Sha256,
        HashAlgorithm::Sha384,
        HashAlgorithm::Sha512,
    });
    assert(result.error.empty());
    assert(result.bytes_read == vector.size());
    assert(result.values.size() == 6);
    assert(format_hex(result.values[0].bytes) == "cbf43926");
    assert(format_hex(result.values[1].bytes) == "25f9e794323b453885f5181f1b624d0b");
    assert(format_hex(result.values[2].bytes) == "f7c3bc1d808e04732adf679965ccc34ca7ae3441");
    assert(format_hex(result.values[3].bytes) == "15e2b0d3c33891ebb0f1ef609ec419420c20e320ce94c65fbc8c3312448eb225");
    assert(format_hex(result.values[4].bytes) == "eb455d56d2c1a69de64e832011f3393d45f3fa31d6842f21af92d2fe469c499da5e3179847334a18479c8d1dedea1be3");
    assert(format_hex(result.values[5].bytes) == "d9e6762dd1c8eaf6d61b3c6192fc408d4d6d5f1176d0c29169bc24e71c3f274ad27fcd5811b313d681f7e55ec02d73d499c95455b6b5bb503acf574fba8ffe85");
    assert(hash_results_equal(result, result));
    auto different = result;
    different.values[1].bytes[0] ^= 0x01u;
    assert(!hash_results_equal(result, different));

    const auto path_two = std::filesystem::temp_directory_path() / "filehash_smoke_abc.txt";
    {
        std::ofstream output(path_two, std::ios::binary | std::ios::trunc);
        output << "abc";
    }
    const auto parallel = hash_files({path, path_two}, {HashAlgorithm::Md5}, 2);
    assert(parallel.size() == 2);
    assert(parallel[0].error.empty() && parallel[1].error.empty());
    assert(format_hex(parallel[0].values[0].bytes) == "25f9e794323b453885f5181f1b624d0b");
    assert(format_hex(parallel[1].values[0].bytes) == "900150983cd24fb0d6963f7d28e17f72");

    const std::atomic<bool> cancel{true};
    const auto cancelled = hash_files({path, path_two}, {HashAlgorithm::Md5}, 2,
                                       [&cancel] { return cancel.load(); });
    assert(cancelled.size() == 2);
    assert(cancelled[0].cancelled && cancelled[1].cancelled);
    assert(cancelled[0].path == path && cancelled[1].path == path_two);

    std::filesystem::remove(path);
    std::filesystem::remove(path_two);

    std::cout << "filehash smoke: core vectors passed\n";
    return 0;
}
