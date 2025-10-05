#include "gentest/runner.h"
using namespace gentest::asserts;

#include <thread>

namespace concurrency {

[[using gentest: test("concurrency/child_expect_pass")]]
void child_expect_pass() {
    auto tok = gentest::ctx::current();
    std::thread t([tok] {
        gentest::ctx::Adopt guard(tok);
        EXPECT_TRUE(true);
        EXPECT_EQ(1, 1);
    });
    t.join();
}

[[using gentest: test("concurrency/child_expect_fail")]]
void child_expect_fail() {
    auto tok = gentest::ctx::current();
    std::thread t([tok] {
        // Intentionally forget to adopt to exercise global fallback
        (void)tok;
        EXPECT_TRUE(false, "child thread EXPECT_TRUE(false)");
        EXPECT_EQ(1, 2, "child thread EXPECT_EQ(1,2)");
    });
    t.join();
}

} // namespace concurrency

namespace concurrency_multi {

[[using gentest: test("concurrency/multi_adopt_expect_pass")]]
void multi_adopt_expect_pass() {
    auto tok = gentest::ctx::current();
    std::thread t1([tok]{ gentest::ctx::Adopt g(tok); EXPECT_TRUE(true); });
    std::thread t2([tok]{ gentest::ctx::Adopt g(tok); EXPECT_EQ(10, 10); });
    std::thread t3([tok]{ gentest::ctx::Adopt g(tok); EXPECT_NE(1, 2); });
    t1.join(); t2.join(); t3.join();
}

[[using gentest: test("concurrency/multi_adopt_expect_fail")]]
void multi_adopt_expect_fail() {
    auto tok = gentest::ctx::current();
    std::thread t1([tok]{ gentest::ctx::Adopt g(tok); EXPECT_TRUE(false, "multi t1"); });
    std::thread t2([tok]{ gentest::ctx::Adopt g(tok); EXPECT_EQ(1, 2, "multi t2"); });
    std::thread t3([tok]{ gentest::ctx::Adopt g(tok); EXPECT_NE(3, 3, "multi t3"); });
    t1.join(); t2.join(); t3.join();
}

[[using gentest: test("concurrency/no_adopt_expect_death_multi")]]
void no_adopt_expect_death_multi() {
    std::thread t1([]{ EXPECT_TRUE(false, "no adopt t1"); });
    std::thread t2([]{ EXPECT_EQ(1, 2, "no adopt t2"); });
    t1.join(); t2.join();
}

} // namespace concurrency_multi

