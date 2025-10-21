// Copyright (c) 2025 Gentest contributors
//
// A thin wrapper that constructs clang::ast_matchers::MatchFinder using
// an ABI-compatible helper compiled with an older C++ standard. This avoids
// the new ABI for std::optional introduced in newer language modes, which
// would otherwise misalign the call convention expected by libclang-cpp
// prebuilt toolchains.

#pragma once

#include <array>
#include <cstddef>
#include <new>
#include <type_traits>

#include <clang/ASTMatchers/ASTMatchFinder.h>

namespace gentest::clang_compat {

// Construct a MatchFinder object in-place at |storage| using the provided
// options. Implemented in match_finder_shim.cpp (compiled with -std=c++17).
void constructMatchFinder(void *storage,
                          const clang::ast_matchers::MatchFinder::MatchFinderOptions &options);

class MatchFinderHolder {
public:
    using MatchFinder = clang::ast_matchers::MatchFinder;
    using Options = MatchFinder::MatchFinderOptions;

    explicit MatchFinderHolder(const Options &options) {
        constructMatchFinder(storage_.data(), options);
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

