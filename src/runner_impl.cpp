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
} // namespace

namespace gentest::detail {
void register_cases(std::span<const Case> cases) {
    auto                       &reg = case_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    reg.cases.insert(reg.cases.end(), cases.begin(), cases.end());
    reg.sorted = false;
}
} // namespace gentest::detail

namespace gentest {
const Case *get_cases() {
    auto                       &reg = case_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    if (!reg.sorted) {
        std::sort(reg.cases.begin(), reg.cases.end(), [](const Case &lhs, const Case &rhs) {
            if (lhs.name != rhs.name)
                return lhs.name < rhs.name;
            if (lhs.file != rhs.file)
                return lhs.file < rhs.file;
            return lhs.line < rhs.line;
        });
        reg.sorted = true;
    }
    return reg.cases.data();
}

std::size_t get_case_count() {
    auto                       &reg = case_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    return reg.cases.size();
}

auto run_all_tests(std::span<const char *> args) -> int {
    gentest::runner::CliOptions opt{};
    if (!gentest::runner::parse_cli(args, opt))
        return 1;

    const Case           *cases      = gentest::get_cases();
    std::size_t           case_count = gentest::get_case_count();
    std::span<const Case> kCases{cases, case_count};
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
