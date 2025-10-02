// Platform/tooling support helpers for clang-tooling invocation.
#pragma once

#include <string>
#include <vector>

#include <clang/Tooling/ArgumentsAdjusters.h>

namespace gentest::codegen {

// Detect host C++ standard library include directories to make clang-tooling
// invocation resilient in environments where the compile database is minimal.
// Returns: a set of directories to be appended via `-isystem` (may be empty).
auto detect_platform_include_dirs() -> std::vector<std::string>;

// Check whether a `-isystem <dir>` pair is already present in a set of args.
bool contains_isystem_entry(const clang::tooling::CommandLineArguments &args, const std::string &dir);

} // namespace gentest::codegen
