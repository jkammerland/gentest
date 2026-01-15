#include <gentest/runner.h>

#include <cstddef>
#include <span>
#include <string_view>

namespace {

void smoke(void *) {}

constexpr gentest::Case kCases[] = {
    {
        .name = "consumer/smoke",
        .fn = &smoke,
        .file = __FILE__,
        .line = 1,
        .is_benchmark = false,
        .is_jitter = false,
        .is_baseline = false,
        .tags = std::span<const std::string_view>{},
        .requirements = std::span<const std::string_view>{},
        .skip_reason = std::string_view{},
        .should_skip = false,
        .fixture = std::string_view{},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite = "consumer",
        .acquire_fixture = nullptr,
    },
};

struct Registrar {
    Registrar() { gentest::detail::register_cases(std::span{kCases}); }
};
[[maybe_unused]] const Registrar kRegistrar{};

} // namespace

int main(int argc, char **argv) { return gentest::run_all_tests(argc, argv); }
