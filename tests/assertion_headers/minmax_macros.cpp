// Regression: gentest's public headers must compile after <windows.h> has
// defined its function-like min and max macros (that is, without NOMINMAX).
// Calls are spelled (std::min)(...) and (T::max)() so the macros can't expand.
//
// On Windows the test includes <windows.h> itself. Elsewhere it defines
// equivalent macros, but only after the standard and fmt headers, because
// libstdc++ is not written to survive them (the Microsoft STL and libc++ are).
// If a gentest header starts including a standard header that is not listed
// below, add it here.
#if defined(_WIN32)
#include <windows.h>
#else
#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <compare>
#include <concepts>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fmt/core.h>
#include <fmt/format.h>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <optional>
#include <ostream>
#include <ranges>
#include <ratio>
#include <set>
#include <source_location>
#include <span>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#include "gentest/async.h"
#include "gentest/bench_util.h"
#include "gentest/benchmark.h"
#include "gentest/detail/generated_runtime.h"
#include "gentest/fixture.h"
#include "gentest/format_value.h"
#include "gentest/framework_events.h"
#include "gentest/log_sink.h"
#include "gentest/matchers.h"
#include "gentest/mock.h"
#include "gentest/notification.h"
#include "gentest/runner.h"
#include "gentest/runner_fmt.h"
#include "gentest/test.h"

#if !defined(min) || !defined(max)
#error "the min and max macros must still be defined here"
#endif

#include <chrono>
#include <string>
#include <vector>

namespace {

using namespace gentest::asserts;

void passing(void *) {
    // Long strings take the truncating branch of the diagnostic quoting.
    const std::string long_text(600, 'x');
    const auto        quoted = gentest::detail::quote_diagnostic_string(long_text);
    EXPECT_TRUE(quoted.size() < long_text.size());

    // Saturating deadline arithmetic uses the clock's min and max.
    using gentest::async::deadline_detail::Clock;
    EXPECT_TRUE(gentest::async::deadline_detail::saturating_clock_duration((std::chrono::hours::max)()) == (Clock::duration::max)());
    EXPECT_TRUE(gentest::async::deadline_detail::saturating_add((Clock::time_point::max)(), std::chrono::seconds{1}) ==
                (Clock::time_point::max)());
}

} // namespace

int main(int argc, char **argv) {
    const gentest::Case cases[]{
        {.name = "minmax_macros/passing", .fn = passing, .suite = "minmax_macros"},
    };
    std::vector<const char *> args(argv, argv + argc);
    return gentest::run_cases(cases, args);
}
