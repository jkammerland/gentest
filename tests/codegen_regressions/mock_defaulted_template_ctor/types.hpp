#pragma once

#include <array>
#include <cstddef>

namespace fixture {

struct DefaultedTemplateCtorTarget {
    template <template <class, std::size_t> class C, class Marker = void>
    explicit DefaultedTemplateCtorTarget(C<int, 2> value) noexcept : size(static_cast<int>(value.size())) {}

    int  size = 0;
    void ping() {}
};

} // namespace fixture
