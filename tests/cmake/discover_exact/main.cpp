#include "gentest/runner.h"

#include <cstdio>
#include <cstdlib>
#include <span>
#include <string_view>
#include <vector>

namespace {

void decode(void *) { gentest::fail("entered decode body"); }
void fallback(void *) { std::puts("entered fallback/decode body"); }

void terminateCase(void *) {
    (void)std::fputs("entered terminating body\n", stderr);
    std::_Exit(23);
}

} // namespace

int main(int argc, char **argv) {
    constexpr std::string_view tags[]{"slow"};
    const gentest::Case        cases[]{
        {.name = "decode", .fn = decode, .tags = tags},
        {.name = "fallback/decode", .fn = fallback, .is_death = std::getenv("GENTEST_PROBE_BECOME_DEATH") != nullptr},
        {.name = "terminate", .fn = terminateCase, .is_death = true},
    };

    // CTest supplies this environment only to the stale entry, after discovery.
    auto inventory = std::span<const gentest::Case>(cases);
    if (std::getenv("GENTEST_PROBE_HIDE_DECODE"))
        inventory = inventory.subspan(1);

    std::vector<const char *> args(argv, argv + argc);
    return gentest::run_cases(inventory, args);
}
