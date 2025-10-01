// Platform/tooling support helpers for clang-tooling invocation
#pragma once

#include <string>
#include <vector>

#include <clang/Tooling/ArgumentsAdjusters.h>

namespace gentest::codegen {

auto detect_platform_include_dirs() -> std::vector<std::string>;
bool contains_isystem_entry(const clang::tooling::CommandLineArguments &args, const std::string &dir);

} // namespace gentest::codegen
