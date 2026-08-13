#include "rich_cases.hpp"

#include <cstdlib>
#include <gentest/bench_util.h>

namespace header_declaration_registration {

void rich_parameter(int value) { gentest::expect(value == 1 || value == 2); }

void rich_rows(int left, int right) { gentest::expect(right == left + 1); }

void rich_range(int value) { gentest::expect(value >= 1 && value <= 3); }

gentest::async_test<void> rich_async() {
    co_await gentest::async::yield();
    gentest::expect(true);
}

void rich_bench() { gentest::doNotOptimizeAway(42); }

void rich_jitter() { gentest::doNotOptimizeAway(42); }

void rich_death() { std::abort(); }

} // namespace header_declaration_registration
