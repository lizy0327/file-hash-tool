#pragma once

#include <cstddef>

namespace filehash::ui {

enum class Language : std::size_t {
    Chinese = 0,
    English,
};

inline constexpr Language language_from_index(const std::size_t index) {
    return index == 1 ? Language::English : Language::Chinese;
}

inline constexpr std::size_t language_index(const Language language) {
    return static_cast<std::size_t>(language);
}

}  // 命名空间 filehash::ui / Namespace filehash::ui
