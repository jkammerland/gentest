#pragma once

#include <array>
#include <cstddef>

namespace fixture {

#define GENTEST_REVIEW_MARKER void

struct MacroDefaultedTemplateCtorTarget {
    template <template <class, std::size_t> class C, class Marker = GENTEST_REVIEW_MARKER>
    explicit MacroDefaultedTemplateCtorTarget(C<int, 2> value) noexcept : size(static_cast<int>(value.size())) {}

    int size = 0;
    void ping() {}
};

#undef GENTEST_REVIEW_MARKER

} // namespace fixture
