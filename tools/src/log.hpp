// Thread-safe stderr logging for gentest_codegen.
#pragma once

#include <fmt/core.h>
#include <llvm/Support/raw_ostream.h>

#include <mutex>
#include <string_view>
#include <utility>

namespace gentest::codegen {

inline std::mutex &errs_mutex() {
    static std::mutex mu;
    return mu;
}

template <typename... Args>
void log_err(fmt::format_string<Args...> format_string, Args &&...args) {
    std::lock_guard<std::mutex> lock(errs_mutex());
    llvm::errs() << fmt::format(format_string, std::forward<Args>(args)...);
}

inline void log_err_raw(std::string_view message) {
    std::lock_guard<std::mutex> lock(errs_mutex());
    llvm::errs() << message;
}

} // namespace gentest::codegen
