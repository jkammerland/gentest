#pragma once

#include <cstddef>

// Cross‑platform utilities to inhibit compiler optimizations in microbenchmarks.
//
// - doNotOptimizeAway(x): Pretend to use x so the compiler keeps computations
//   that produced it. Overloads handle const and non‑const references.
// - clobberMemory(): Acts as a compiler barrier on memory so the compiler
//   cannot reorder memory operations across it.
//
// These are intended for hot loops inside [[using gentest: bench(...)]] or
// [[using gentest: jitter(...)]] bodies.

namespace gentest {

#if defined(_MSC_VER)

// MSVC/clang-cl: use compiler barriers. Inline asm is not supported on x64.
// _ReadWriteBarrier prevents reordering of loads/stores across the barrier.
extern "C" void _ReadWriteBarrier();
#pragma intrinsic(_ReadWriteBarrier)

template <class T>
inline void doNotOptimizeAway(T const& value) {
    // Create a volatile read to an address derived from value so that
    // the compiler must assume value is needed.
    auto p = reinterpret_cast<char const volatile*>(&value);
    (void)*p;
    _ReadWriteBarrier();
}

template <class T>
inline void doNotOptimizeAway(T& value) {
    auto p = reinterpret_cast<char volatile*>(&value);
    *p = *p; // self read/write through volatile to inhibit SROA
    _ReadWriteBarrier();
}

inline void clobberMemory() { _ReadWriteBarrier(); }

#elif defined(__GNUC__) || defined(__clang__)

template <class T>
inline void doNotOptimizeAway(T const& value) {
    // Keep value live while allowing either register or memory constraints.
    // Avoid "+g" style tied operands that are rejected by some clang targets.
    asm volatile("" : : "r,m"(value) : "memory");
}

template <class T>
inline void doNotOptimizeAway(T& value) {
    // Match const-overload constraints; a memory clobber is sufficient for our
    // benchmark harness and is accepted across clang/gcc targets.
    asm volatile("" : : "r,m"(value) : "memory");
}

inline void clobberMemory() { asm volatile("" : : : "memory"); }

#else

// Fallbacks: best‑effort volatile tricks.
template <class T>
inline void doNotOptimizeAway(T const& value) {
    auto p = reinterpret_cast<char const volatile*>(&value);
    (void)*p;
}

template <class T>
inline void doNotOptimizeAway(T& value) {
    auto p = reinterpret_cast<char volatile*>(&value);
    *p = *p;
}

inline void clobberMemory() { (void)0; }

#endif

} // namespace gentest
