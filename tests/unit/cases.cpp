#include "gentest/attributes.h"
#include "gentest/process.h"
#include "gentest/runner.h"
using namespace gentest::asserts;

#include <array>
#include <chrono>
#include <cstdlib>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>

namespace unit {

inline void throw_runtime_error() { throw std::runtime_error("boom"); }
inline void no_throw() {}

namespace {
constexpr const char *kChildEnv = "GENTEST_SUBPROCESS_CHILD";

bool is_child_process() {
    const char *value = std::getenv(kChildEnv);
    return value != nullptr && value[0] != '\0';
}

bool build_child_options(std::string_view name, gentest::process::SubprocessOptions &options) {
    std::string executable = gentest::process::current_executable_path();
    if (executable.empty()) {
        return false;
    }
    options.argv = {std::move(executable), std::string("--run-test=unit/") + std::string(name)};
    options.env.push_back({kChildEnv, "1"});
    return true;
}
} // namespace

[[using gentest: test("arithmetic/sum"), fast]]
void sum_is_computed() {
    std::array values{1, 2, 3, 4};
    const auto result = std::accumulate(values.begin(), values.end(), 0);
    EXPECT_EQ(values.size(), std::size_t{4});
    ASSERT_EQ(values.front(), 1, "first element");
    EXPECT_EQ(values.back(), 4, "last element");
    const auto average = static_cast<double>(result) / values.size();
    EXPECT_EQ(result, 10);
    EXPECT_EQ(average, 2.5, "arithmetic mean");
}

[[using gentest: test("approx/absolute")]]
void approx_absolute() {
    using gentest::approx::Approx;
    EXPECT_EQ(3.1415, Approx(3.14).abs(0.01));
    EXPECT_EQ(Approx(10.0).abs(0.5), 10.3);
}

[[using gentest: test("approx/relative")]]
void approx_relative() {
    using gentest::approx::Approx;
    EXPECT_EQ(101.0, Approx(100.0).rel(2.0));   // 1% diff within 2%
    EXPECT_EQ(Approx(200.0).rel(1.0), 198.5);  // 0.75% diff within 1%
}

[[using gentest: test("approx/relative_negative")]]
void approx_relative_negative() {
    using gentest::approx::Approx;
    EXPECT_EQ(-101.0, Approx(-100.0).rel(2.0));  // 1% diff within 2%
    EXPECT_EQ(Approx(-200.0).rel(1.0), -198.5);  // 0.75% diff within 1%
}

[[using gentest: test("strings/concatenate"), req("#42"), slow]]
void concatenate_strings() {
    std::string greeting = "hello";
    EXPECT_EQ(greeting.size(), std::size_t{5}, "initial size");
    greeting += " world";
    ASSERT_EQ(greeting.size(), std::size_t{11}, "post-concat size");
    EXPECT_EQ(greeting.substr(0, 5), "hello", "prefix");
    EXPECT_EQ(greeting.substr(6), "world", "suffix");
    EXPECT_TRUE(greeting == "hello world");
}

[[using gentest: test("conditions/negate"), linux]]
void negate_condition() {
    bool flag = false;
    ASSERT_EQ(flag, false, "starts false");
    EXPECT_TRUE(!flag);
    EXPECT_NE(flag, true);

    flag = !flag;
    ASSERT_TRUE(flag, "negation flips to true");
    EXPECT_EQ(flag, true, "flag now true");

    flag = !flag;
    EXPECT_TRUE(!flag);
    EXPECT_EQ(flag, false, "double negation");
}

[[using gentest: test("conditions/false_and_relations")]]
void false_and_relations() {
    EXPECT_FALSE(false);
    ASSERT_FALSE(false, "still false");

    EXPECT_LT(1, 2);
    EXPECT_LE(2, 2);
    EXPECT_GT(2, 1);
    EXPECT_GE(2, 2);

    ASSERT_LT(1, 2);
    ASSERT_LE(2, 2);
    ASSERT_GT(2, 1);
    ASSERT_GE(2, 2);
}

[[using gentest: fast]]
void default_name_free() {
    EXPECT_TRUE(true);
}

[[using gentest: test("exceptions/expect_throw")]]
void expect_throw_simple() {
    EXPECT_THROW(throw_runtime_error(), std::runtime_error);
    EXPECT_THROW(throw 123, int);
}

[[using gentest: test("exceptions/expect_no_throw")]]
void expect_no_throw_simple() {
    EXPECT_NO_THROW(no_throw());
}

[[using gentest: test("exceptions/assert_throw")]]
void assert_throw_simple() {
    ASSERT_THROW(throw_runtime_error(), std::runtime_error);
    EXPECT_TRUE(true, "continues after ASSERT_THROW");
}

[[using gentest: test("exceptions/assert_no_throw")]]
void assert_no_throw_simple() {
    ASSERT_NO_THROW(no_throw());
    EXPECT_TRUE(true, "continues after ASSERT_NO_THROW");
}

struct DefaultNameFixture {
    [[using gentest: fast]]
    void default_name_member() {
        EXPECT_TRUE(true);
    }
};

[[using gentest: test("process/child_pass")]]
void process_child_pass() {
    if (!is_child_process()) {
        EXPECT_TRUE(true);
        return;
    }
    EXPECT_TRUE(true);
}

[[using gentest: test("process/child_fail")]]
void process_child_fail() {
    if (!is_child_process()) {
        EXPECT_TRUE(true);
        return;
    }
    EXPECT_TRUE(false, "intentional failure");
}

[[using gentest: test("process/child_sleep")]]
void process_child_sleep() {
    if (!is_child_process()) {
        EXPECT_TRUE(true);
        return;
    }
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(200ms);
    EXPECT_TRUE(true);
}

[[using gentest: test("process/spawn_pass")]]
void process_spawn_pass() {
    gentest::process::SubprocessOptions options;
    ASSERT_TRUE(build_child_options("process/child_pass", options), "missing current executable");
    const auto result = gentest::process::run_subprocess(options);
    EXPECT_TRUE(result.started);
    EXPECT_FALSE(result.timed_out);
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.exit_code, 0);
}

[[using gentest: test("process/spawn_fail")]]
void process_spawn_fail() {
    gentest::process::SubprocessOptions options;
    ASSERT_TRUE(build_child_options("process/child_fail", options), "missing current executable");
    const auto result = gentest::process::run_subprocess(options);
    EXPECT_TRUE(result.started);
    EXPECT_FALSE(result.timed_out);
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.exit_code != 0);
}

[[using gentest: test("process/spawn_timeout")]]
void process_spawn_timeout() {
    using namespace std::chrono_literals;
    gentest::process::SubprocessOptions options;
    ASSERT_TRUE(build_child_options("process/child_sleep", options), "missing current executable");
    options.timeout = 50ms;
    const auto result = gentest::process::run_subprocess(options);
    EXPECT_TRUE(result.started);
    EXPECT_TRUE(result.timed_out);
}

} // namespace unit
