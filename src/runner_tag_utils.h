#pragma once

#include "gentest/runner.h"

#include <string_view>

namespace gentest::runner {

inline bool iequals_ascii(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const char a = lhs[i];
        const char b = rhs[i];
        if (a == b) {
            continue;
        }
        const char al = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
        const char bl = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
        if (al != bl) {
            return false;
        }
    }
    return true;
}

inline bool has_tag_ci(const gentest::Case &test, std::string_view tag) {
    for (auto candidate : test.tags) {
        if (iequals_ascii(candidate, tag)) {
            return true;
        }
    }
    return false;
}

} // namespace gentest::runner
