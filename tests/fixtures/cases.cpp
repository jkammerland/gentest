#include "gentest/attributes.h"
#include "gentest/fixture.h"
#include "gentest/runner.h"

#include <memory>
#include <vector>
#include <stdexcept>

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

struct AllocEphemeral {
    static inline int allocations = 0;
    static std::unique_ptr<AllocEphemeral> gentest_allocate() {
        ++allocations;
        return std::make_unique<AllocEphemeral>();
    }

    [[using gentest: test("ephemeral/alloc_hook")]]
    void alloc_hook() {
        gentest::expect_eq(allocations, 1, "allocation hook runs for ephemeral fixtures");
    }
};

struct [[using gentest: fixture(suite)]] Counter /* optionally implement setup/teardown later */ {
    int x = 0;

    [[using gentest: test("stateful/a_set_flag")]]
    void set_flag() {
        x = 1;
    }

    [[using gentest: test("stateful/b_check_flag")]]
    void check_flag() {
        gentest::expect_eq(x, 1, "state preserved across methods");
    }
};

struct [[using gentest: fixture(suite)]] SuiteAlloc {
    static inline int allocations = 0;
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

struct [[using gentest: fixture(global)]] GlobalCounter {
    int hits = 0;

    [[using gentest: test("global/increment")]]
    void increment() {
        ++hits;
        gentest::expect_eq(hits, 1, "first increment sets global state");
    }

    [[using gentest: test("global/observe")]]
    void observe() {
        gentest::expect_eq(hits, 1, "global fixture persists across tests");
    }
};

struct [[using gentest: fixture(global)]] GlobalAlloc {
    static inline int allocations = 0;
    static std::shared_ptr<GlobalAlloc> gentest_allocate() {
        ++allocations;
        return std::make_shared<GlobalAlloc>();
    }

    int hits = 0;

    [[using gentest: test("global_alloc/a_increment")]]
    void increment() {
        ++hits;
        gentest::expect_eq(allocations, 1, "global fixture allocated once");
    }

    [[using gentest: test("global_alloc/b_observe")]]
    void observe() {
        gentest::expect_eq(hits, 1, "global fixture persists across tests");
        gentest::expect_eq(allocations, 1, "global fixture allocated once");
    }
};

// Free-function fixtures composed via attribute

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
    static inline int allocations = 0;
    static std::unique_ptr<PtrFixture> gentest_allocate() {
        ++allocations;
        return std::make_unique<PtrFixture>();
    }
    int value = 3;
};

struct SharedFixture {
    static inline int allocations = 0;
    static std::shared_ptr<SharedFixture> gentest_allocate() {
        ++allocations;
        return std::make_shared<SharedFixture>();
    }
    int value = 4;
};

[[using gentest: test("free/basic"), fixtures(A, B<int>, C)]]
void free_basic(A &a, B<int> &b, C &c) {
    // setUp must have run for A
    gentest::expect_eq(a.phase, 1, "A setUp ran");
    a.phase = 2; // allow tearDown to validate
    gentest::expect(b.x == 0, "B default value");
    gentest::expect(std::string(b.msg) == "ok", "B default value");
    gentest::expect_eq(c.v, 7, "C default value");
}

[[using gentest: test("free/pointer"), fixtures(PtrFixture)]]
void free_pointer(PtrFixture *fx) {
    gentest::expect(fx != nullptr, "fixture pointer is valid");
    gentest::expect_eq(fx->value, 3, "fixture state available");
    gentest::expect_eq(PtrFixture::allocations, 1, "allocation hook runs for pointer fixture");
}

[[using gentest: test("free/shared_ptr"), fixtures(SharedFixture)]]
void free_shared_ptr(std::shared_ptr<SharedFixture> fx) {
    gentest::expect(static_cast<bool>(fx), "shared fixture pointer is valid");
    gentest::expect_eq(fx->value, 4, "fixture state available");
    gentest::expect_eq(SharedFixture::allocations, 1, "allocation hook runs for shared fixture");
}

} // namespace fixtures
