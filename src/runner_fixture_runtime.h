#pragma once

#include <string>
#include <thread>
#include <vector>

namespace gentest::runner::detail {

struct SharedFixtureRuntimeSession {
    bool            owns_gate     = false;
    bool            gate_rejected = false;
    std::thread::id owner_thread{};
};

bool setup_shared_fixture_runtime(std::vector<std::string> &errors, SharedFixtureRuntimeSession &session);
bool teardown_shared_fixture_runtime(std::vector<std::string> &errors, SharedFixtureRuntimeSession &session);

} // namespace gentest::runner::detail
