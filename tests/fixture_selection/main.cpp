#include "cases.hpp"
#include "gentest/detail/generated_runtime.h"

#include <cstdio>
#include <span>
#include <string_view>
#include <vector>

namespace {

using fixture_selection::counts;

bool check(bool condition, std::string_view message) {
    if (!condition)
        (void)std::fprintf(stderr, "fixture selection contract: %.*s\n", static_cast<int>(message.size()), message.data());
    return condition;
}

int run(std::initializer_list<const char *> options) {
    std::vector<const char *> args{
        "gentest", "--no-color", "--bench-epochs=1", "--bench-warmup=0", "--bench-min-epoch-time-s=0", "--bench-min-total-time-s=0"};
    args.insert(args.end(), options.begin(), options.end());
    counts = {};
    return gentest::run_all_tests(args);
}

bool shared_lifecycle() {
    return check(counts.suite_setup == 1 && counts.suite_teardown == 1, "suite lifecycle runs exactly once") &&
           check(counts.dependency_setup == 1 && counts.dependency_teardown == 1, "transitive dependency runs exactly once") &&
           check(counts.unrelated_allocations == 0 && counts.unrelated_setup == 0, "unrelated fixture was never activated") &&
           check(counts.destruction_order == std::vector<int>{1, 2}, "dependent must be destroyed before its dependency");
}

void manual_case(void *ctx) {
    auto *fixture = static_cast<fixture_selection::ASelected *>(ctx);
    gentest::expect(fixture && fixture->dependency && fixture->dependency->alive, "manual case activates its shared dependencies");
    ++counts.bodies;
}

void measured_lookup(void *) {
    if (gentest::detail::bench_phase() != gentest::detail::BenchPhase::Call)
        return;

    std::string error;
    auto        fixture = gentest::detail::get_shared_fixture(gentest::detail::SharedFixtureScope::Suite, "fixture_selection",
                                                              "fixture_selection::ASelected", error);
    gentest::expect(!fixture && error.find("during a measured call") != std::string::npos,
                    "new shared fixtures must be acquired before a timing loop");
    ++counts.bodies;
}

gentest::detail::AsyncTaskPtr blocked_async(void *) {
    gentest::fail("blocked/static-skip async wrapper must not run");
    return {};
}

} // namespace

int main() {
    // Each invocation resets activation state while the process-wide generated
    // registry remains intact. A fixture-free selection has no environment setup.
    if (!check(run({"--run=fixture_selection/plain"}) == 0, "plain selection succeeds") ||
        !check(counts.bodies == 1 && counts.suite_setup == 0 && counts.dependency_setup == 0 && counts.unrelated_allocations == 0 &&
                   counts.unrelated_setup == 0,
               "plain selection constructs no shared fixture"))
        return 1;

    if (!check(run({"--run=fixture_selection/selected", "--repeat=3"}) == 0, "repeated generated selection succeeds") ||
        !shared_lifecycle() ||
        !check(counts.bodies == 3 && counts.local_setup == 3 && counts.local_teardown == 3,
               "local fixtures repeat; shared fixtures persist"))
        return 2;

    if (!check(run({"--filter=fixture_selection/selected*", "--repeat=2", "--shuffle", "--seed=7"}) == 0,
               "mixed async selection succeeds") ||
        !shared_lifecycle() || !check(counts.bodies == 4, "sync and async cases each repeat twice"))
        return 3;

    for (const char *name :
         {"--run=fixture_selection/static_skip", "--run=fixture_selection/skipped_bench", "--run=fixture_selection/skipped_jitter"}) {
        if (!check(run({name}) == 0, "statically skipped selection succeeds") ||
            !check(counts.unrelated_allocations == 0 && counts.unrelated_setup == 0 && counts.bodies == 0,
                   "static skip avoids shared fixture acquisition"))
            return 4;
    }

    if (!check(run({"--run=fixture_selection/runtime_skip"}) == 0, "runtime skipped selection succeeds") || !shared_lifecycle() ||
        !check(counts.bodies == 0, "runtime skip still tears down acquired fixtures"))
        return 5;

    for (const char *name : {"--run=fixture_selection/measured", "--run=fixture_selection/latency"}) {
        if (!check(run({name}) == 0, "measured selection succeeds") || !shared_lifecycle() ||
            !check(counts.bodies > 0, "measurement body actually ran"))
            return 6;
    }

    if (!check(run({"--run=fixture_selection/nested_failure"}) == 1, "dependency activation preserves parent setup failure") ||
        !shared_lifecycle() || !check(counts.bodies == 0, "failed parent setup blocks its case"))
        return 11;

    const gentest::Case late_lookup{
        .name         = "manual/measured_lookup",
        .fn           = &measured_lookup,
        .is_benchmark = true,
        .benchmark    = {.execution = gentest::BenchmarkExecution::Once},
    };
    counts = {};
    const char *lookup_args[]{"--no-color"};
    if (!check(gentest::run_cases(std::span(&late_lookup, 1), lookup_args) == 0, "measured lookup policy is enforced") ||
        !check(counts.suite_setup == 0 && counts.dependency_setup == 0 && counts.bodies == 1, "measured call cannot activate a fixture"))
        return 12;

    // Explicit fixture metadata is sufficient for embedding callers. Static
    // skips remain skips even in a fixture group whose runnable case is blocked.
    gentest::Case manual[]{
        {.name             = "manual/selected",
         .fn               = &manual_case,
         .fixture          = "fixture_selection::ASelected",
         .fixture_lifetime = gentest::FixtureLifetime::MemberSuite,
         .suite            = "fixture_selection"},
    };
    const char *args[]{"--no-color", "--repeat=2"};
    counts = {};
    if (!check(gentest::run_cases(manual, args) == 0, "manual case table succeeds") || !shared_lifecycle() ||
        !check(counts.bodies == 2, "manual case body repeats"))
        return 7;

    manual[0].should_skip = true;
    counts                = {};
    if (!check(gentest::run_cases(manual, args) == 0, "manual static skip succeeds") ||
        !check(counts.suite_setup == 0 && counts.bodies == 0, "manual static skip acquires no shared fixture"))
        return 8;

    if (!check(run({"--run=fixture_selection/unrelated", "--repeat=2"}) == 1, "activated failing fixture fails its run") ||
        !check(counts.unrelated_allocations == 1 && counts.unrelated_setup == 1, "failed activation is attempted only once per run"))
        return 9;

    for (const bool async : {false, true}) {
        auto skipped        = manual[0];
        skipped.name        = async ? "manual/async_static_skip" : "manual/sync_static_skip";
        skipped.fixture     = "fixture_selection::Unrelated";
        skipped.async_fn    = async ? &blocked_async : nullptr;
        skipped.is_async    = async;
        auto blocked        = skipped;
        blocked.name        = async ? "manual/async_unavailable" : "manual/sync_unavailable";
        blocked.should_skip = false;
        const gentest::Case grouped[]{skipped, blocked};
        counts = {};
        if (!check(gentest::run_cases(grouped, args) == 1, "failed mixed group fails the run") ||
            !check(counts.unrelated_allocations == 1 && counts.unrelated_setup == 1 && counts.bodies == 0,
                   "mixed group activates failed fixture once"))
            return 10;
    }

    std::puts("fixture selection lifecycle contract passed");
    return 0;
}
