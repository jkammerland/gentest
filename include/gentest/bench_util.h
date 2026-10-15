#pragma once

#include "gentest/detail/runtime_export.h"

#include <type_traits>

#if !defined(__GNUC__) && !defined(__clang__)
#include <atomic>
#include <memory>
#endif

// Compiler barriers for benchmark bodies, not thread synchronization.
// doNotOptimizeAway observes the complete object, not memory reached through
// its pointers. It does not prevent constant folding of known computations.
// Use runtime inputs and escape separately owned result storage when needed.
namespace gentest {

namespace detail {

// Native MSVC has no GNU-style operands. The runtime defines this opaque call
// in a separate, non-LTO translation unit; it never retains or dereferences pointers.
GENTEST_RUNTIME_API void escape_benchmark_value(void const volatile *value) noexcept;

template <class T> inline void observe_benchmark_value(T &value) {
#if defined(__GNUC__) || defined(__clang__)
    // clang-cl supports GNU-style asm too: do not select MSVC's fallback first.
    if constexpr (std::is_scalar_v<T> && !std::is_const_v<T>) {
#if defined(__clang__)
        asm volatile("" : "+r,m"(value) : : "memory");
#else
        asm volatile("" : "+m,r"(value) : : "memory");
#endif
    } else {
        // A complete memory operand preserves every member without pretending
        // to assign to const members, nonassignable objects, or their arrays.
        asm volatile("" : : "m"(value) : "memory");
    }
#else
    // Escaping the address to an unknown callee preserves the entire object.
    // Reading only its first byte would allow other value dependencies to die.
    escape_benchmark_value(std::addressof(value));
    std::atomic_signal_fence(std::memory_order_acq_rel);
#endif
}

} // namespace detail

template <class T> inline void doNotOptimizeAway(T const &value) { detail::observe_benchmark_value(value); }

template <class T> inline void doNotOptimizeAway(T &value) { detail::observe_benchmark_value(value); }

template <class T> inline void doNotOptimizeAway(T &&value) { detail::observe_benchmark_value(value); }

// Prevent compiler movement of memory operations; this is not a CPU fence.
inline void clobberMemory() {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : : "memory");
#else
    std::atomic_signal_fence(std::memory_order_acq_rel);
#endif
}

} // namespace gentest
