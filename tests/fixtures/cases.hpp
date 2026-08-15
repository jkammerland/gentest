#pragma once

#include "gentest/attributes.h"
#include "gentest/fixture.h"
#include "gentest/runner.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace fixtures {

struct StackFixture {
    std::vector<int> data;

    [[using gentest: test("ephemeral/size_zero")]]
    void size_zero() {
        gentest::expect_eq(data.size(), std::size_t{0}, "fresh instance has size 0");
    }

    [[using gentest: test("ephemeral/push_pop")]]
    void push_pop() {
        data.push_back(1);
        gentest::expect_eq(data.back(), 1, "push stores value");
        data.pop_back();
        gentest::expect_eq(data.size(), std::size_t{0}, "pop restores size");
    }
};

using StackFixtureSharedAlias = std::shared_ptr<StackFixture>;

struct AllocEphemeral {
    static inline int                      allocations = 0;
    static std::unique_ptr<AllocEphemeral> gentest_allocate() {
        ++allocations;
        return std::make_unique<AllocEphemeral>();
    }

    [[using gentest: test("ephemeral/alloc_hook")]]
    void alloc_hook(StackFixture &helper) {
        gentest::expect_eq(allocations, 1, "allocation hook runs for ephemeral fixtures");
        gentest::expect_eq(helper.data.size(), std::size_t{0}, "member test receives inferred fixture parameter");
    }
};

struct [[using gentest: fixture(suite)]] Counter /* optionally implement setup/teardown later */ {
    int                    x    = 0;
    static inline Counter *last = nullptr;

    [[using gentest: test("stateful/a_set_flag")]]
    void set_flag() {
        x    = 1;
        last = this;
    }

    [[using gentest: test("stateful/b_check_flag")]]
    void check_flag(StackFixture &helper) {
        gentest::expect_eq(x, 1, "state preserved across methods");
        gentest::expect_eq(this, last, "suite fixture instance reused");
        gentest::expect_eq(helper.data.size(), std::size_t{0}, "suite member test receives inferred fixture parameter");
    }

    [[using gentest: test("stateful/c_default_ptr_passthrough")]]
    void default_ptr_passthrough(StackFixture *helper = nullptr) {
        gentest::expect(helper == nullptr, "defaulted fixture-like pointer parameter is passed through (not fixture-inferred)");
    }

    [[using gentest: test("stateful/d_default_shared_ptr_alias_passthrough")]]
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    void default_shared_ptr_alias_passthrough(StackFixtureSharedAlias helper = nullptr) {
        gentest::expect(!helper, "defaulted shared_ptr fixture-like alias parameter is passed through (not fixture-inferred)");
    }
};

struct [[using gentest: fixture(suite)]] SuiteAlloc {
    static inline int                  allocations = 0;
    static std::unique_ptr<SuiteAlloc> gentest_allocate() {
        ++allocations;
        return std::make_unique<SuiteAlloc>();
    }

    int value = 0;

    [[using gentest: test("stateful_alloc/a_set_value")]]
    void set_value() {
        value = 5;
        gentest::expect_eq(allocations, 1, "suite fixture allocated once");
    }

    [[using gentest: test("stateful_alloc/b_check_value")]]
    void check_value() {
        gentest::expect_eq(value, 5, "suite fixture state persists");
        gentest::expect_eq(allocations, 1, "suite fixture allocated once");
    }
};

struct [[using gentest: fixture(suite)]] SuiteHook {
    static inline int         allocations = 0;
    static inline std::string seen_suite;
    static inline SuiteHook  *first = nullptr;

    static std::unique_ptr<SuiteHook> gentest_allocate(std::string_view suite) {
        ++allocations;
        seen_suite = std::string(suite);
        return std::make_unique<SuiteHook>();
    }

    [[using gentest: test("suite_hook/a_allocate")]]
    void allocate() {
        if (!first)
            first = this;
        gentest::expect_eq(allocations, 1, "suite fixture allocated once");
        gentest::expect_eq(seen_suite, "fixtures", "suite name passed to allocation hook");
    }

    [[using gentest: test("suite_hook/b_shared")]]
    void shared() {
        gentest::expect_eq(this, first, "suite fixture instance reused");
        gentest::expect_eq(allocations, 1, "suite fixture allocated once");
        gentest::expect_eq(seen_suite, "fixtures", "suite name passed to allocation hook");
    }
};

struct [[using gentest: fixture(global)]] GlobalCounter {
    int                          hits = 0;
    static inline GlobalCounter *last = nullptr;

    [[using gentest: test("global/increment")]]
    void increment() {
        ++hits;
        last = this;
        gentest::expect_eq(hits, 1, "first increment sets global state");
    }

    [[using gentest: test("global/observe")]]
    void observe() {
        gentest::expect_eq(hits, 1, "global fixture persists across tests");
        gentest::expect_eq(this, last, "global fixture instance reused");
    }
};

struct [[using gentest: fixture(global)]] GlobalAlloc {
    static inline int                   allocations = 0;
    static inline GlobalAlloc          *last        = nullptr;
    static std::shared_ptr<GlobalAlloc> gentest_allocate() {
        ++allocations;
        return std::make_shared<GlobalAlloc>();
    }

    int hits = 0;

    [[using gentest: test("global_alloc/a_increment")]]
    void increment() {
        ++hits;
        last = this;
        gentest::expect_eq(allocations, 1, "global fixture allocated once");
    }

    [[using gentest: test("global_alloc/b_observe")]]
    void observe() {
        gentest::expect_eq(hits, 1, "global fixture persists across tests");
        gentest::expect_eq(this, last, "global fixture instance reused");
        gentest::expect_eq(allocations, 1, "global fixture allocated once");
    }
};

struct [[using gentest: fixture(global)]] GlobalBothAlloc {
    static inline int         no_arg_allocations    = 0;
    static inline int         suite_arg_allocations = 0;
    static inline std::string seen_suite;

    static std::unique_ptr<GlobalBothAlloc> gentest_allocate() {
        ++no_arg_allocations;
        return std::make_unique<GlobalBothAlloc>();
    }

    static std::unique_ptr<GlobalBothAlloc> gentest_allocate(std::string_view suite) {
        ++suite_arg_allocations;
        seen_suite = std::string(suite);
        return std::make_unique<GlobalBothAlloc>();
    }

    [[using gentest: test("global_alloc_both/no_arg_preferred")]]
    void no_arg_preferred() {
        gentest::expect_eq(no_arg_allocations, 1, "global fixture prefers no-arg allocation when both hooks exist");
        gentest::expect_eq(suite_arg_allocations, 0, "global fixture does not call suite-aware allocation when no-arg exists");
        gentest::expect_eq(seen_suite, std::string{}, "global fixture did not pass an empty suite to the suite-aware hook");
    }
};

// Free-function fixtures inferred from function parameter types.

struct A : gentest::FixtureSetup, gentest::FixtureTearDown {
    int  phase = 0;
    void setUp() override {
        gentest::expect_eq(phase, 0, "A::setUp before test");
        phase = 1;
    }
    void tearDown() override {
        gentest::expect_eq(phase, 2, "A::tearDown after test");
        phase = 3;
    }
};

template <typename T> struct B {
    const char *msg = "ok";
    T           x   = T{};
};
class C {
  public:
    int v = 7;
};

struct PtrFixture {
    static inline int                  allocations = 0;
    static inline std::string          seen_suite;
    static std::unique_ptr<PtrFixture> gentest_allocate(std::string_view suite) {
        ++allocations;
        seen_suite = std::string(suite);
        return std::make_unique<PtrFixture>();
    }
    int value = 3;
};

struct RawFixture {
    static inline int  allocations = 0;
    static RawFixture *gentest_allocate() {
        ++allocations;
        return new RawFixture();
    }
    int value = 5;
};

struct RawDerivedFixture;

struct RawPolymorphicFixture {
    static inline int base_deletes    = 0;
    static inline int derived_deletes = 0;

    static RawDerivedFixture *gentest_allocate();

    ~RawPolymorphicFixture() { ++base_deletes; }

    int value = 6;
};

struct RawDerivedFixture : RawPolymorphicFixture {
    ~RawDerivedFixture() { ++derived_deletes; }
};

inline RawDerivedFixture *RawPolymorphicFixture::gentest_allocate() { return new RawDerivedFixture(); }

struct SharedFixture {
    static inline int                     allocations = 0;
    static std::shared_ptr<SharedFixture> gentest_allocate() {
        ++allocations;
        return std::make_shared<SharedFixture>();
    }
    int value = 4;
};

using SharedFixtureHandle   = std::shared_ptr<SharedFixture>;
using PtrFixtureSharedAlias = std::shared_ptr<PtrFixture>;

struct CustomDeleterFixture {
    struct Deleter {
        void operator()(CustomDeleterFixture *ptr) const {
            ++CustomDeleterFixture::deletes;
            delete ptr;
        }
    };
    static inline int                                     deletes = 0;
    static std::unique_ptr<CustomDeleterFixture, Deleter> gentest_allocate() {
        return std::unique_ptr<CustomDeleterFixture, Deleter>(new CustomDeleterFixture(), Deleter{});
    }

    [[using gentest: test("custom_deleter/a_use")]]
    void use() {
        gentest::expect_eq(deletes, 0, "deleter not called before first test");
    }

    [[using gentest: test("custom_deleter/b_after_first")]]
    void after_first() {
        gentest::expect_eq(deletes, 1, "deleter ran after first test");
    }

    [[using gentest: test("custom_deleter/c_after_second")]]
    void after_second() {
        gentest::expect_eq(deletes, 2, "deleter ran after second test");
    }
};

[[using gentest: test("free/basic")]]
void free_basic(A &a, B<int> &b, C &c, int marker = 7);

[[using gentest: test("free/default_ptr_passthrough")]]
void free_default_ptr_passthrough(PtrFixture *fx = nullptr);

[[using gentest: test("free/default_shared_ptr_alias_passthrough")]]
// NOLINTNEXTLINE(performance-unnecessary-value-param)
void free_default_shared_ptr_alias_passthrough(PtrFixtureSharedAlias fx = nullptr);

[[using gentest: test("free/pointer")]]
void free_pointer(PtrFixture *fx);

[[using gentest: test("free/raw_pointer")]]
void free_raw_pointer(RawFixture *fx);

[[using gentest: test("free/raw_pointer_polymorphic_a_use")]]
void free_raw_pointer_polymorphic_a_use(RawPolymorphicFixture *fx);

[[using gentest: test("free/raw_pointer_polymorphic_b_after_first")]]
void free_raw_pointer_polymorphic_b_after_first(RawPolymorphicFixture *fx);

[[using gentest: test("free/shared_ptr")]]
// NOLINTNEXTLINE(performance-unnecessary-value-param)
void free_shared_ptr(SharedFixtureHandle fx);

namespace suite_shared {
struct [[using gentest: fixture(suite)]] SharedSuiteFx : gentest::FixtureSetup, gentest::FixtureTearDown {
    static inline int            setups    = 0;
    static inline int            teardowns = 0;
    static inline SharedSuiteFx *first     = nullptr;
    static inline bool           saw_test  = false;
    int                          value     = 0;

    void setUp() override { ++setups; }
    void tearDown() override {
        ++teardowns;
        gentest::expect_eq(teardowns, 1, "suite fixture tearDown runs once");
        if (!saw_test) {
            // Focused runs may not execute any case that uses this shared fixture.
            return;
        }
    }
};

using SharedSuiteAlias = SharedSuiteFx;

namespace inner_a {
[[using gentest: test("suite_shared/inner_a/set")]]
void set(SharedSuiteFx &fx);
} // namespace inner_a

namespace inner_b {
[[using gentest: test("suite_shared/inner_b/check")]]
void check(SharedSuiteAlias &fx);
} // namespace inner_b
} // namespace suite_shared

namespace global_shared {
struct [[using gentest: fixture(global)]] SharedGlobalFx : gentest::FixtureSetup, gentest::FixtureTearDown {
    static inline int             setups    = 0;
    static inline int             teardowns = 0;
    static inline SharedGlobalFx *first     = nullptr;
    static inline bool            saw_test  = false;
    int                           hits      = 0;

    void setUp() override { ++setups; }
    void tearDown() override {
        ++teardowns;
        gentest::expect_eq(teardowns, 1, "global fixture tearDown runs once");
        if (!saw_test) {
            // Focused runs may not execute any case that uses this shared fixture.
            return;
        }
    }
};

using SharedGlobalAlias    = std::shared_ptr<SharedGlobalFx>;
using SharedGlobalRawAlias = SharedGlobalFx *;

namespace inner_a {
[[using gentest: test("global_shared/inner_a/hit")]]
void hit(SharedGlobalFx &fx);
} // namespace inner_a

namespace inner_b {
[[using gentest: test("global_shared/inner_b/check")]]
// NOLINTNEXTLINE(performance-unnecessary-value-param)
void check(SharedGlobalAlias fx);
} // namespace inner_b

namespace inner_c {
[[using gentest: test("global_shared/inner_c/pointer")]]
void pointer(SharedGlobalRawAlias fx);
} // namespace inner_c
} // namespace global_shared

namespace mixed_suite {
struct LocalMix : gentest::FixtureSetup, gentest::FixtureTearDown {
    static inline int setups    = 0;
    static inline int teardowns = 0;
    int               value     = 0;

    void setUp() override { ++setups; }
    void tearDown() override { ++teardowns; }
};

struct [[using gentest: fixture(suite)]] SuiteMix : gentest::FixtureSetup {
    static inline int       setups      = 0;
    static inline SuiteMix *first       = nullptr;
    static inline bool      initialized = false;
    int                     value       = 0;

    void setUp() override { ++setups; }
};

struct [[using gentest: fixture(global)]] GlobalMix : gentest::FixtureSetup {
    static inline int        setups      = 0;
    static inline GlobalMix *first       = nullptr;
    static inline bool       initialized = false;
    int                      value       = 0;

    void setUp() override { ++setups; }
};

using SuiteMixAlias   = SuiteMix;
using GlobalMixHandle = std::shared_ptr<GlobalMix>;

[[using gentest: test("mixed/one")]]
void mixed_one(LocalMix &local, SuiteMix &suite, GlobalMix &global);

[[using gentest: test("mixed/two"), parameters(marker, 9, 11, 13)]]
// NOLINTNEXTLINE(performance-unnecessary-value-param)
void mixed_two(LocalMix &local, int marker, SuiteMixAlias &suite, GlobalMixHandle global);
} // namespace mixed_suite

} // namespace fixtures
