#pragma once

#include <array>
#include <cstddef>

namespace fixture {

struct TemplateTemplateCtorTarget {
    template <template <class, std::size_t> class C>
    explicit TemplateTemplateCtorTarget(C<int, 2> value) noexcept : size(static_cast<int>(value.size())) {}

    int size = 0;
    void ping() {}
};

} // namespace fixture
