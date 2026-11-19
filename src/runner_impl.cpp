#include "death_watchdog.h"
#include "gentest/detail/registration_runtime.h"
#include "gentest/detail/registry_runtime.h"
#include "gentest/detail/runtime_context.h"
#include "gentest/runner.h"
#include "runner_cli.h"
#include "runner_orchestrator.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <span>
#include <vector>

namespace {
thread_local const std::filesystem::path *death_test_marker = nullptr;

struct DeathTestProtocolScope {
    const std::filesystem::path *previous = death_test_marker;
    explicit DeathTestProtocolScope(const std::filesystem::path *path) { death_test_marker = path; }
    bool recordSkip(std::string_view name) const {
        std::ofstream result(*death_test_marker, std::ios::binary | std::ios::trunc);
        result << name << "\nskipped";
        result.close();
        death_test_marker = nullptr;
        return static_cast<bool>(result);
    }
    ~DeathTestProtocolScope() {
        gentest::detail::notify_case_body_finish();
        death_test_marker = previous;
    }
};

struct CaseRegistry {
    std::vector<gentest::Case> cases;
    bool                       sorted = false;
    std::mutex                 mtx;
};

auto case_registry() -> CaseRegistry & {
    static CaseRegistry reg;
    return reg;
}

void sort_cases(std::vector<gentest::Case> &cases) {
    std::ranges::sort(cases, [](const gentest::Case &lhs, const gentest::Case &rhs) {
        if (lhs.name != rhs.name)
            return lhs.name < rhs.name;
        if (lhs.file != rhs.file)
            return lhs.file < rhs.file;
        return lhs.line < rhs.line;
    });
}

auto sorted_case_copy(std::span<const gentest::Case> cases) -> std::vector<gentest::Case> {
    std::vector<gentest::Case> sorted(cases.begin(), cases.end());
    sort_cases(sorted);
    return sorted;
}

auto snapshot_cases() -> std::vector<gentest::Case> {
    auto                       &reg = case_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    if (!reg.sorted) {
        sort_cases(reg.cases);
        reg.sorted = true;
    }
    return reg.cases;
}

auto run_sorted_cases(std::span<const gentest::Case> cases, const gentest::runner::CliOptions &parsed,
                      const gentest::RunConfiguration &configuration) -> int {
    std::filesystem::path marker;

#if defined(_WIN32)
    // Only a process launched by the supervisor requests its original Unicode arguments.
    auto        options = parsed;
    std::string exact_name;
    if (options.death_wide) {
        for (const auto &arg : gentest::detail::wideProcessArguments()) {
            const std::string_view value = arg;
            if (value.starts_with("--run="))
                exact_name = value.substr(6);
            constexpr std::string_view prefix = "--gentest-death-marker=";
            if (value.starts_with(prefix)) {
                const auto bytes = value.substr(prefix.size());
                marker           = std::u8string(bytes.begin(), bytes.end());
            }
        }
        if (exact_name.empty() || marker.empty())
            return 1;
        options.run_name = exact_name.c_str();
    }
#else
    const auto &options = parsed;
#endif
    if (marker.empty() && options.death_test_marker)
        marker = options.death_test_marker;
    auto watched = configuration;
    if (options.death_watchdog) {
        try {
            auto path        = marker;
            watched.notifier = gentest::detail::deathWatchdogNotifier(path += ".watchdog", configuration.notifier);
        } catch (const std::exception &e) {
            (void)std::fprintf(stderr, "Cannot attach death-test watchdog: %s\n", e.what());
            return 1;
        }
    }
    gentest::detail::FrameworkRunScope events(watched);
    DeathTestProtocolScope             protocol(options.death_test_marker ? &marker : nullptr);
    const int                          result = gentest::runner::run_from_options(cases, options);
    events.finish();
    if (events.failed())
        return 1;
    // Only a normal runner return can confirm a skip. Setup and teardown can
    // exit with the same status as a real skip without returning here.
    if (options.death_test_marker && options.skip_return_code != 0 && result == options.skip_return_code &&
        !protocol.recordSkip(options.run_name))
        return 1;
    return result;
}
} // namespace

namespace gentest::detail {
void notify_case_body_start() {
    if (!death_test_marker)
        return;

    const auto    context = current_test();
    std::ofstream marker(*death_test_marker, std::ios::binary | std::ios::trunc);
    if (context)
        marker << context->display_name;
    marker.close();
    if (!context || !marker)
        throw gentest::failure("cannot write death-test body-entry marker");
}

void notify_case_body_finish() noexcept {
    if (!death_test_marker)
        return;
    std::ofstream marker(*death_test_marker, std::ios::binary | std::ios::app);
    marker << "\ncompleted";
}

void register_cases(std::span<const Case> cases) {
    auto                       &reg = case_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    reg.cases.insert(reg.cases.end(), cases.begin(), cases.end());
    reg.sorted = false;
}

auto snapshot_registered_cases() -> std::vector<Case> { return snapshot_cases(); }
} // namespace gentest::detail

namespace gentest {
auto registered_cases() -> std::vector<Case> { return snapshot_cases(); }

auto run_cases(std::span<const Case> cases, std::span<const char *> args) -> int { return run_cases(cases, args, {}); }

auto run_cases(std::span<const Case> cases, std::span<const char *> args, const RunConfiguration &configuration) -> int {
    gentest::runner::CliOptions opt{};
    if (!gentest::runner::parse_cli(args, opt))
        return 1;

    const auto sorted_cases = sorted_case_copy(cases);
    return run_sorted_cases(sorted_cases, opt, configuration);
}

auto run_all_tests(std::span<const char *> args) -> int { return run_all_tests(args, {}); }

auto run_all_tests(std::span<const char *> args, const RunConfiguration &configuration) -> int {
    gentest::runner::CliOptions opt{};
    if (!gentest::runner::parse_cli(args, opt))
        return 1;

    const auto cases = gentest::detail::snapshot_registered_cases();
    return run_sorted_cases(cases, opt, configuration);
}

auto run_all_tests(int argc, char **argv) -> int { return run_all_tests(argc, argv, {}); }

auto run_all_tests(int argc, char **argv, const RunConfiguration &configuration) -> int {
    std::vector<const char *> a;
    a.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i)
        a.push_back(argv[i]);
    return run_all_tests(std::span<const char *>{a.data(), a.size()}, configuration);
}

} // namespace gentest
