#pragma once

#include "gentest/detail/runtime_context.h"
#include "gentest/runner.h"

#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace gentest::runner {

bool acquire_case_fixture(const gentest::Case &c, void *&ctx, std::string &reason);

} // namespace gentest::runner

namespace gentest::runner::detail {

struct SharedFixtureRuntimeSession {
    bool                                                        owns_gate     = false;
    bool                                                        gate_rejected = false;
    std::thread::id                                             owner_thread{};
    std::optional<gentest::detail::DefaultStdoutLogWriterScope> diagnostic_log_route;
};

bool setup_shared_fixture_runtime(std::vector<std::string> &errors, SharedFixtureRuntimeSession &session, bool machine_report);
bool teardown_shared_fixture_runtime(std::vector<std::string> &errors, SharedFixtureRuntimeSession &session);

} // namespace gentest::runner::detail
