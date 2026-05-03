#include "runner_test_executor.h"

#include "runner_async_executor.h"
#include "runner_case_result.h"
#include "runner_fixture_runtime.h"
#include "runner_test_plan.h"

#include <string>
#include <utility>
#include <vector>

namespace gentest::runner {
namespace {

bool should_stop_after_failure(bool fail_fast, const TestCounters &counters) {
    return fail_fast && (counters.failures > 0 || counters.blocked > 0);
}

bool run_tests_sync(TestRunContext &state, std::span<const gentest::Case> cases, std::span<const SuiteExecutionPlan> plans, bool fail_fast,
                    TestCounters &counters) {
    for (const auto &plan : plans) {
        for (auto i : plan.free_like) {
            execute_and_record(state, cases[i], nullptr, counters);
            if (should_stop_after_failure(fail_fast, counters)) {
                return true;
            }
        }

        const auto run_groups = [&](const std::vector<gentest::runner::FixtureGroupPlan> &groups) -> bool {
            for (const auto &group : groups) {
                void       *group_ctx = nullptr;
                std::string group_reason;
                if (!group.idxs.empty() && !gentest::runner::acquire_case_fixture(cases[group.idxs.front()], group_ctx, group_reason)) {
                    const std::string msg = shared_fixture_unavailable_message(group.fixture, std::move(group_reason));
                    for (auto i : group.idxs) {
                        record_synthetic_skip(state, cases[i], msg, counters, true);
                        if (should_stop_after_failure(fail_fast, counters)) {
                            return true;
                        }
                    }
                    continue;
                }

                for (auto i : group.idxs) {
                    execute_and_record(state, cases[i], group_ctx, counters);
                    if (should_stop_after_failure(fail_fast, counters)) {
                        return true;
                    }
                }
            }
            return false;
        };

        if (run_groups(plan.suite_groups)) {
            return true;
        }
        if (run_groups(plan.global_groups)) {
            return true;
        }
    }

    return false;
}

} // namespace

bool run_tests_once(TestRunContext &state, std::span<const gentest::Case> cases, std::span<const SuiteExecutionPlan> plans, bool fail_fast,
                    TestCounters &counters) {
    if (plans_include_async_cases(cases, plans)) {
        return run_tests_async_batch(state, cases, plans, fail_fast, counters);
    }
    return run_tests_sync(state, cases, plans, fail_fast, counters);
}

} // namespace gentest::runner
