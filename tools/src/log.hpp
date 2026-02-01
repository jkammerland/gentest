// Thread-safe stderr logging for gentest_codegen.
#pragma once

#include <fmt/format.h>
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
    fmt::memory_buffer buffer;
    buffer.reserve(256);
    fmt::format_to(std::back_inserter(buffer), format_string, std::forward<Args>(args)...);
    llvm::errs() << fmt::to_string(buffer);
}

inline void log_err_raw(std::string_view message) {
    std::lock_guard<std::mutex> lock(errs_mutex());
    llvm::errs() << message;
}

} // namespace gentest::codegen
