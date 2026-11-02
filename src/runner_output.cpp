#include "runner_output.h"

#include <cstdio>
#include <fmt/format.h>

namespace gentest::runner {
namespace {
thread_local bool failure_logs_enabled = false;
}

FailureLogScope::FailureLogScope(bool enabled) noexcept : previous_(failure_logs_enabled) { failure_logs_enabled = enabled; }

FailureLogScope::~FailureLogScope() { failure_logs_enabled = previous_; }

bool capture_failure_logs() noexcept { return failure_logs_enabled; }

void print_failure_logs(std::span<const std::string> logs) {
    if (logs.empty()) {
        return;
    }
    fmt::print(stderr, "Captured logs:\n");
    for (const auto &line : logs) {
        fmt::print(stderr, "  {}\n", line);
    }
}

void print_fixture_failure_logs(const gentest::detail::TestContextInfo &ctx) {
    if (capture_failure_logs()) {
        // Fixture users have drained and their local buffers have been flushed.
        print_failure_logs(ctx.logs);
    }
}

} // namespace gentest::runner
