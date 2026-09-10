#pragma once

#include "core/hash_engine.h"

#include <sstream>
#include <string>

namespace filehash::ui {

inline std::string format_result_values(const HashFileResult& result) {
    if (!result.error.empty()) return result.error;
    if (result.cancelled) return "Cancelled";

    std::ostringstream output;
    bool first = true;
    for (const auto& value : result.values) {
        if (!first) output << " | ";
        first = false;
        output << algorithm_name(value.algorithm) << '=' << format_hex(value.bytes);
    }
    return output.str();
}

inline std::string format_result_lines(const HashFileResult& result) {
    if (!result.error.empty()) return "Error: " + result.error;
    if (result.cancelled) return "Status: Cancelled";
    if (result.values.empty()) return "Status: No result";

    std::ostringstream output;
    for (const auto& value : result.values) {
        output << algorithm_name(value.algorithm) << ": " << format_hex(value.bytes) << '\n';
    }
    const std::string text = output.str();
    return text.empty() ? "Status: No result" : text.substr(0, text.size() - 1);
}

}  // 命名空间 filehash::ui / Namespace filehash::ui
