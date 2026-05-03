#pragma once

#include <type_traits>

namespace gentest {

#ifdef GENTEST_CODEGEN
namespace detail::mocking {
template <typename T, typename = void> struct PlaceholderMockBase {
    PlaceholderMockBase() = default;

    // Allow constructing codegen-only placeholder mocks with arbitrary
    // arguments so definitions can mention mocks for non-default-constructible
    // targets while the real generated mock is not available yet.
    template <typename... Args> explicit PlaceholderMockBase(Args &&...) {}

    ~PlaceholderMockBase() = default;
};

template <typename T> struct PlaceholderMockBase<T, std::void_t<decltype(sizeof(T)), std::enable_if_t<!std::is_abstract_v<T>>>> : T {
    using T::T;
};
} // namespace detail::mocking

template <typename T> struct mock : detail::mocking::PlaceholderMockBase<T> {
    using GentestTarget = T;
    using Base          = detail::mocking::PlaceholderMockBase<T>;

    using Base::Base;

    GentestTarget *operator&() {
        if constexpr (std::is_base_of_v<GentestTarget, detail::mocking::PlaceholderMockBase<T>>) {
            return static_cast<GentestTarget *>(this);
        } else {
            return reinterpret_cast<GentestTarget *>(this);
        }
    }

    const GentestTarget *operator&() const {
        if constexpr (std::is_base_of_v<GentestTarget, detail::mocking::PlaceholderMockBase<T>>) {
            return static_cast<const GentestTarget *>(this);
        } else {
            return reinterpret_cast<const GentestTarget *>(this);
        }
    }
};
#else
template <typename T> struct mock;
#endif

} // namespace gentest
