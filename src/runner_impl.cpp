#include "gentest/detail/registration_runtime.h"
#include "gentest/runner.h"
#include "runner_cli.h"
#include "runner_orchestrator.h"

#include <algorithm>
#include <mutex>
#include <span>
#include <string_view>
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

constexpr unsigned kGeneratedCaseBenchmarkFlag = 1u << 0u;
constexpr unsigned kGeneratedCaseJitterFlag    = 1u << 1u;
constexpr unsigned kGeneratedCaseBaselineFlag  = 1u << 2u;
constexpr unsigned kGeneratedCaseSkipFlag      = 1u << 3u;

auto make_span(gentest::detail::GeneratedStringRange range) -> std::span<const std::string_view> { return {range.data, range.size}; }

auto make_case(const gentest::detail::GeneratedCase &generated) -> gentest::Case {
    return gentest::Case{
        .name             = generated.name,
        .fn               = generated.fn,
        .file             = generated.file,
        .line             = generated.line,
        .is_benchmark     = (generated.flags & kGeneratedCaseBenchmarkFlag) != 0u,
        .is_jitter        = (generated.flags & kGeneratedCaseJitterFlag) != 0u,
        .is_baseline      = (generated.flags & kGeneratedCaseBaselineFlag) != 0u,
        .tags             = make_span(generated.tags),
        .requirements     = make_span(generated.requirements),
        .skip_reason      = generated.skip_reason,
        .should_skip      = (generated.flags & kGeneratedCaseSkipFlag) != 0u,
        .fixture          = generated.fixture,
        .fixture_lifetime = generated.fixture_lifetime,
        .suite            = generated.suite,
        .simple_fn        = generated.simple_fn,
    };
}
} // namespace

namespace gentest::detail {
void register_cases(std::span<const Case> cases) {
    auto                       &reg = case_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    reg.cases.insert(reg.cases.end(), cases.begin(), cases.end());
    reg.sorted = false;
}

void register_generated_cases(const GeneratedCase *cases, std::size_t count) {
    auto                       &reg = case_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    reg.cases.reserve(reg.cases.size() + count);
    for (std::size_t idx = 0; idx < count; ++idx) {
        reg.cases.push_back(make_case(cases[idx]));
    }
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
