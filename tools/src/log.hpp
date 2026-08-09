// Thread-safe stderr logging for gentest_codegen.
#pragma once

#include <fmt/format.h>
#include <llvm/Support/raw_ostream.h>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace gentest::codegen {

inline std::mutex &errs_mutex() {
    static std::mutex mu;
    return mu;
}

namespace detail {

inline thread_local std::string *thread_log_capture = nullptr;

inline void write_log_message(std::string_view message) {
    if (thread_log_capture != nullptr) {
        thread_log_capture->append(message);
        return;
    }

    std::lock_guard<std::mutex> lock(errs_mutex());
    llvm::errs() << message;
}

} // namespace detail

class ScopedLogCapture {
  public:
    explicit ScopedLogCapture(std::string &buffer) : previous_(detail::thread_log_capture) { detail::thread_log_capture = &buffer; }

    ScopedLogCapture(const ScopedLogCapture &)            = delete;
    ScopedLogCapture &operator=(const ScopedLogCapture &) = delete;

    ~ScopedLogCapture() { detail::thread_log_capture = previous_; }

  private:
    std::string *previous_ = nullptr;
};

template <typename... Args> void log_err(fmt::format_string<Args...> format_string, Args &&...args) {
    fmt::memory_buffer buffer;
    buffer.reserve(256);
    fmt::format_to(std::back_inserter(buffer), format_string, std::forward<Args>(args)...);
    detail::write_log_message(std::string_view{buffer.data(), buffer.size()});
}

inline void log_err_raw(std::string_view message) { detail::write_log_message(message); }

} // namespace gentest::codegen
