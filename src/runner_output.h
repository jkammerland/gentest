#pragma once

#include "gentest/detail/runtime_context.h"

#include <span>
#include <string>

namespace gentest::runner {

class FailureLogScope {
  public:
    explicit FailureLogScope(bool enabled) noexcept;
    FailureLogScope(const FailureLogScope &)            = delete;
    FailureLogScope &operator=(const FailureLogScope &) = delete;
    ~FailureLogScope();

  private:
    bool previous_ = false;
};

bool capture_failure_logs() noexcept;
void print_failure_logs(std::span<const std::string> logs);
void print_fixture_failure_logs(const gentest::detail::TestContextInfo &ctx);

} // namespace gentest::runner
