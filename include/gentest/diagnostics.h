#pragma once

#include "gentest/detail/runtime_export.h"
#include "gentest/format_value.h"

#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace gentest {
namespace detail {
struct DiagnosticContextEntry;
struct DiagnosticContextState;
} // namespace detail

// A lexical diagnostic scope belongs to the current owning test/coroutine.
class [[nodiscard]] DiagnosticContext {
  public:
    GENTEST_RUNTIME_API explicit DiagnosticContext(std::string message, const std::source_location &location);
    GENTEST_RUNTIME_API ~DiagnosticContext();
    DiagnosticContext(const DiagnosticContext &)            = delete;
    DiagnosticContext &operator=(const DiagnosticContext &) = delete;
    DiagnosticContext(DiagnosticContext &&)                 = delete;
    DiagnosticContext &operator=(DiagnosticContext &&)      = delete;

  private:
    std::shared_ptr<detail::DiagnosticContextState>       state_;
    std::shared_ptr<const detail::DiagnosticContextEntry> entry_;
};

[[nodiscard]] inline auto diagnostic_context(std::string_view            message,
                                             const std::source_location &location = std::source_location::current()) -> DiagnosticContext {
    return DiagnosticContext(std::string(message), location);
}

template <typename T>
[[nodiscard]] auto diagnostic_context(std::string_view name, const T &value,
                                      const std::source_location &location = std::source_location::current()) -> DiagnosticContext {
    return DiagnosticContext(fmt::format("{}={}", name, format_value(value)), location);
}

} // namespace gentest
