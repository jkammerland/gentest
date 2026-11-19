#include "gentest/runner.h"

#include <cstdio>
#include <cstdlib>
#include <span>
#include <string_view>
#include <vector>

namespace {

void decode(void *) { gentest::fail("entered decode body"); }
void fallback(void *) { std::puts("entered fallback/decode body"); }

} // namespace

int main(int argc, char **argv) {
    constexpr std::string_view tags[]{"slow"};
    const gentest::Case        cases[]{
        {.name = "decode", .fn = decode, .tags = tags},
        {.name = "fallback/decode", .fn = fallback},
    };

    // CTest supplies this environment only to the stale entry, after discovery.
    auto inventory = std::span<const gentest::Case>(cases);
    if (std::getenv("GENTEST_PROBE_HIDE_DECODE"))
        inventory = inventory.subspan(1);

    std::vector<const char *> args(argv, argv + argc);
    return gentest::run_cases(inventory, args);
}
