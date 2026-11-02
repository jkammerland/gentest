#include "gentest/detail/generated_runtime.h"
#include "gentest/log_sink.h"
#include "gentest/runner.h"

#include <array>
#include <csignal>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace gentest::asserts;

volatile std::sig_atomic_t trap_count     = 0;
volatile std::sig_atomic_t in_assertion   = 0;
volatile std::sig_atomic_t misplaced_trap = 0;

void pass(void *) { gentest::log("passing body marker"); }

void fail(void *) {
    gentest::log("failing body marker");
    auto        context = gentest::get_current_context();
    std::thread worker([context] {
        auto adoption = gentest::set_current_context(context);
        gentest::log("failing worker marker");
    });
    worker.join();

    in_assertion = 1;
    EXPECT_EQ(1, 2, "workflow deliberate failure");
    in_assertion = 0;
}

void skip(void *) { gentest::skip("workflow skip"); }

void xfail(void *) {
    gentest::log("expected failure marker");
    gentest::xfail("workflow expected failure");
    EXPECT_TRUE(false);
}

void xpass(void *) {
    gentest::log("unexpected pass marker");
    gentest::xfail("workflow unexpected pass");
}

gentest::awaitable<void> async_case(bool fails) {
    gentest::log(fails ? "failing async before yield" : "passing async before yield");
    co_await gentest::async::yield();
    gentest::log(fails ? "failing async after yield" : "passing async after yield");
    if (fails) {
        in_assertion = 1;
        EXPECT_TRUE(false, "workflow deliberate async failure");
        in_assertion = 0;
    }
}

auto async_pass(void *) -> gentest::detail::AsyncTaskPtr { return gentest::detail::make_async_task(async_case(false)); }
auto async_fail(void *) -> gentest::detail::AsyncTaskPtr { return gentest::detail::make_async_task(async_case(true)); }

void measured(void *) {
    switch (gentest::detail::bench_phase()) {
    case gentest::detail::BenchPhase::Setup: gentest::log("measured setup marker"); break;
    case gentest::detail::BenchPhase::Call: gentest::log("measured body marker"); break;
    case gentest::detail::BenchPhase::Teardown:
        gentest::log("measured teardown marker");
        EXPECT_TRUE(false, "measured teardown failure");
        break;
    case gentest::detail::BenchPhase::None: break;
    }
}

auto test_case(std::string_view name, void (*body)(void *)) -> gentest::Case {
    return {.name = name, .fn = body, .file = __FILE__, .line = __LINE__, .suite = "workflow"};
}

auto workflow_cases() -> std::vector<gentest::Case> {
    std::vector<gentest::Case> cases{
        test_case("workflow/sync/pass", &pass),
        test_case(R"case(workflow/sync/fail[param='quote', text="$() ; * ? \"])case", &fail),
        test_case("workflow/sync/skip", &skip),
        test_case("workflow/sync/xfail", &xfail),
        test_case("workflow/sync/xpass", &xpass),
        test_case("workflow/async/pass", &pass),
        test_case("workflow/async/fail[parameter=\"a b\"]", &pass),
        test_case("workflow/measured/failure", &measured),
        test_case("--seed", &pass),
        test_case("duplicates/name", &pass),
        test_case("duplicates/name", &fail),
    };
    cases[5].async_fn            = &async_pass;
    cases[5].is_async            = true;
    cases[6].async_fn            = &async_fail;
    cases[6].is_async            = true;
    cases[7].is_benchmark        = true;
    cases[7].benchmark.execution = gentest::BenchmarkExecution::Once;
    return cases;
}

// Each phase has enough cases to observe shuffle changes without interleaving.
auto shuffle_cases(std::vector<std::string> &names) -> std::vector<gentest::Case> {
    constexpr std::array group_names{"free", "suite_a", "suite_b", "global_a", "global_b"};
    constexpr std::array lifetimes{
        gentest::FixtureLifetime::None,         gentest::FixtureLifetime::MemberSuite,  gentest::FixtureLifetime::MemberSuite,
        gentest::FixtureLifetime::MemberGlobal, gentest::FixtureLifetime::MemberGlobal,
    };
    names.reserve(group_names.size() * 8);
    for (const auto *group : group_names) {
        for (int index = 0; index < 8; ++index) {
            names.push_back(std::string("shuffle/") + group + "/" + std::to_string(index));
        }
    }

    std::vector<gentest::Case> cases;
    for (std::size_t group = 0; group < group_names.size(); ++group) {
        for (std::size_t index = 0; index < 8; ++index) {
            auto test             = test_case(names[group * 8 + index], +[](void *) {});
            test.suite            = "shuffle";
            test.fixture_lifetime = lifetimes[group];
            if (group != 0) {
                test.fixture = group_names[group];
            }
            cases.push_back(test);
        }
    }
    return cases;
}

void register_shuffle_fixtures() {
    const auto create = +[](std::string_view, std::string &) -> std::shared_ptr<void> { return std::make_shared<int>(1); };
    for (auto name : {"suite_a", "suite_b"}) {
        gentest::detail::register_shared_fixture(gentest::detail::SharedFixtureScope::Suite, "shuffle", name, create, nullptr, nullptr);
    }
    for (auto name : {"global_a", "global_b"}) {
        gentest::detail::register_shared_fixture(gentest::detail::SharedFixtureScope::Global, "shuffle", name, create, nullptr, nullptr);
    }
}

auto restoration_probe(const char *binary) -> int {
    std::ostringstream custom_logs;
    auto               sink  = gentest::add_log_sink(gentest::make_ostream_log_sink(custom_logs));
    const auto         cases = workflow_cases();
    const char        *captured[]{binary, "--filter=workflow/async/*", "--output=failures-only", "--repeat=2", "--no-color"};
    if (gentest::run_cases(cases, captured) != 1) {
        return 81;
    }
    for (auto marker :
         {"passing async before yield", "passing async after yield", "failing async before yield", "failing async after yield"}) {
        const auto text  = custom_logs.str();
        const auto first = text.find(marker);
        if (first == std::string::npos || text.find(marker, first + 1) == std::string::npos) {
            return 82;
        }
    }
    (void)sink.remove();

    const char *ordinary[]{binary, "--run=workflow/sync/pass", "--no-color"};
    return gentest::run_cases(cases, ordinary);
}

auto debugger_probe(const char *binary) -> int {
    const auto  cases = workflow_cases();
    const char *enabled_pass[]{binary, "--run=workflow/sync/pass", "--break-on-failure", "--no-color"};
    if (gentest::run_cases(cases, enabled_pass) != 0) {
        return 83;
    }

#if defined(SIGTRAP) && !defined(_MSC_VER)
    const auto previous_handler = std::signal(
        SIGTRAP, +[](int) {
            trap_count = trap_count + 1;
            if (!in_assertion) {
                misplaced_trap = 1;
            }
        });
    const char *enabled_fail[]{binary, "--filter=workflow/*/fail*", "--kind=test", "--break-on-failure", "--repeat=2", "--no-color"};
    const auto  result = gentest::run_cases(cases, enabled_fail);
    if (result != 1 || trap_count != 4 || misplaced_trap) {
        (void)std::signal(SIGTRAP, previous_handler);
        return 84;
    }

    // A rejected nested invocation must restore its owner's debugger policy.
    auto nested = test_case(
        "nested/owner", +[](void *) {
            auto        inner = test_case("nested/inner", &pass);
            const char *inner_args[]{"gentest", "--break-on-failure", "--no-color"};
            EXPECT_EQ(gentest::run_cases(std::span{&inner, 1}, inner_args), 1);
            in_assertion = 1;
            EXPECT_TRUE(false, "nested owner failure");
            in_assertion = 0;
        });
    const char *nested_enabled[]{binary, "--break-on-failure", "--no-color"};
    const auto  nested_enabled_result = gentest::run_cases(std::span{&nested, 1}, nested_enabled);
    const char *nested_disabled[]{binary, "--no-color"};
    const auto  nested_disabled_result = gentest::run_cases(std::span{&nested, 1}, nested_disabled);
    (void)std::signal(SIGTRAP, previous_handler);
    if (nested_enabled_result != 1 || nested_disabled_result != 1 || trap_count != 5 || misplaced_trap) {
        return 86;
    }
#endif

    // A subsequent ordinary failure must not retain the earlier trap policy.
    const auto  sync_failure = cases[1];
    const char *ordinary[]{binary, "--no-color"};
    return gentest::run_cases(std::span{&sync_failure, 1}, ordinary) == 1 ? 0 : 85;
}

auto blocked_probe(const char *binary) -> int {
    const auto create = +[](std::string_view, std::string &reason) -> std::shared_ptr<void> {
        gentest::log("fixture allocation marker");
        reason = "deliberate unavailable fixture";
        return {};
    };
    gentest::detail::register_shared_fixture(gentest::detail::SharedFixtureScope::Suite, "blocked", "unavailable", create, nullptr,
                                             nullptr);
    auto test             = test_case("blocked/case", &pass);
    test.suite            = "blocked";
    test.fixture          = "unavailable";
    test.fixture_lifetime = gentest::FixtureLifetime::MemberSuite;
    const char *args[]{binary, "--output=failures-only", "--no-color"};
    return gentest::run_cases(std::span{&test, 1}, args);
}

} // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--probe=restoration") {
        return restoration_probe(argv[0]);
    }
    if (argc == 2 && std::string_view(argv[1]) == "--probe=debugger") {
        return debugger_probe(argv[0]);
    }
    if (argc == 2 && std::string_view(argv[1]) == "--probe=blocked") {
        return blocked_probe(argv[0]);
    }

    std::vector<const char *> args(argv, argv + argc);
    std::vector<std::string>  shuffle_names;
    auto                      cases    = workflow_cases();
    auto                      shuffled = shuffle_cases(shuffle_names);
    cases.insert(cases.end(), shuffled.begin(), shuffled.end());
    register_shuffle_fixtures();
    return gentest::run_cases(cases, args);
}
