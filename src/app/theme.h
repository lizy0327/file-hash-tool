#pragma once

#include <array>
#include <cstddef>

namespace filehash::ui {

enum class ThemeId : std::size_t {
    ArcticBlue = 0,
    MidnightCyan,
    WarmOrange,
    JadeMist,
    VioletCloud,
    GraphiteAmber,
    Count,
};

struct ThemeInfo {
    ThemeId id;
    const char* name;
    const char* slug;
    bool dark;
};

inline constexpr std::array<ThemeInfo, static_cast<std::size_t>(ThemeId::Count)> kThemes{{
    {ThemeId::ArcticBlue, "Arctic Blue", "arctic-blue", false},
    {ThemeId::MidnightCyan, "Midnight Cyan", "midnight-cyan", true},
    {ThemeId::WarmOrange, "Warm Orange", "warm-orange", false},
    {ThemeId::JadeMist, "Jade Mist", "jade-mist", false},
    {ThemeId::VioletCloud, "Violet Cloud", "violet-cloud", false},
    {ThemeId::GraphiteAmber, "Graphite Amber", "graphite-amber", true},
}};

inline constexpr ThemeId theme_from_index(const std::size_t index) {
    return index < kThemes.size() ? static_cast<ThemeId>(index) : ThemeId::ArcticBlue;
}

inline constexpr std::size_t theme_index(const ThemeId theme) {
    const auto index = static_cast<std::size_t>(theme);
    return index < kThemes.size() ? index : 0;
}

inline constexpr const ThemeInfo& theme_info(const ThemeId theme) {
    return kThemes[theme_index(theme)];
}

}  // namespace filehash::ui
