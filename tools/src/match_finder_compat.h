// Copyright (c) 2025 Gentest contributors
//
// A thin wrapper that constructs clang::ast_matchers::MatchFinder using
// an ABI-compatible helper compiled with an older C++ standard. This avoids
// the new ABI for std::optional introduced in newer language modes, which
// would otherwise misalign the call convention expected by libclang-cpp
// prebuilt toolchains. The price we pay is that we manually name-link the
// constructor symbol; bumps to LLVM/Clang that rename or inline it will cause
// link errors until we refresh the mangled name, and the initializer runs
// with the older libstdc++ ABI (no C++23 optional optimisations).

#pragma once

#include <array>
#include <cstddef>
#include <new>
#include <type_traits>

#include <clang/ASTMatchers/ASTMatchFinder.h>

namespace gentest::clang_compat {

// Construct a MatchFinder object in-place at |storage| with default options.
// Implemented in match_finder_shim.cpp (compiled with -std=c++17) to ensure we
// always negotiate the libstdc++17 ABI used by prebuilt libclang toolchains.
void constructMatchFinder(void *storage);

class MatchFinderHolder {
public:
    using MatchFinder = clang::ast_matchers::MatchFinder;

    MatchFinderHolder() {
        constructMatchFinder(storage_.data());
    }

    MatchFinderHolder(const MatchFinderHolder &) = delete;
    MatchFinderHolder &operator=(const MatchFinderHolder &) = delete;

    ~MatchFinderHolder() {
        ptr()->~MatchFinder();
    }

    MatchFinder *operator->() { return ptr(); }
    MatchFinder &get() { return *ptr(); }

private:
    using Storage = std::array<std::byte, sizeof(MatchFinder)>;

    MatchFinder *ptr() {
        auto *raw = reinterpret_cast<MatchFinder *>(storage_.data());
        return std::launder(raw);
    }

    Storage storage_{};
};

}  // namespace gentest::clang_compat
