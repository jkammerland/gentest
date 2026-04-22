#include "gentest/detail/registration_runtime.h"
#include "gentest/detail/registry_runtime.h"
#include "gentest/runner.h"
#include "runner_cli.h"
#include "runner_orchestrator.h"

#include <algorithm>
#include <mutex>
#include <span>
#include <vector>

namespace {
struct CaseRegistry {
    std::vector<gentest::Case> cases;
    bool                       sorted = false;
    std::mutex                 mtx;
};

auto case_registry() -> CaseRegistry & {
    static CaseRegistry reg;
    return reg;
}

auto snapshot_cases() -> std::vector<gentest::Case> {
    auto                       &reg = case_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    if (!reg.sorted) {
        std::ranges::sort(reg.cases, [](const gentest::Case &lhs, const gentest::Case &rhs) {
            if (lhs.name != rhs.name)
                return lhs.name < rhs.name;
            if (lhs.file != rhs.file)
                return lhs.file < rhs.file;
            return lhs.line < rhs.line;
        });
        reg.sorted = true;
    }
    return reg.cases;
}
} // namespace

namespace gentest::detail {
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

auto run_all_tests(std::span<const char *> args) -> int {
    gentest::runner::CliOptions opt{};
    if (!gentest::runner::parse_cli(args, opt))
        return 1;

    const auto            cases = gentest::detail::snapshot_registered_cases();
    std::span<const Case> kCases{cases};
    return gentest::runner::run_from_options(kCases, opt);
}

auto run_all_tests(int argc, char **argv) -> int {
    std::vector<const char *> a;
    a.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i)
        a.push_back(argv[i]);
    return run_all_tests(std::span<const char *>{a.data(), a.size()});
}

} // namespace gentest
