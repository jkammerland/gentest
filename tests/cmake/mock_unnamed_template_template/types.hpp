#pragma once

#include <array>
#include <cstddef>

namespace fixture {

struct UnnamedTemplateTemplateMethodTarget {
    template <template <class, std::size_t> class>
    void take(std::array<int, 2> value) {
        (void)value;
    }
};

} // namespace fixture
