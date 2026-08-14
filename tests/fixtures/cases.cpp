#include "cases.hpp"

namespace fixtures {

void free_basic(A &a, B<int> &b, C &c, int marker) {
    // setUp must have run for A
    gentest::expect_eq(a.phase, 1, "A setUp ran");
    a.phase = 2; // allow tearDown to validate
    gentest::expect(b.x == 0, "B default value");
    gentest::expect(std::string(b.msg) == "ok", "B default value");
    gentest::expect_eq(c.v, 7, "C default value");
    gentest::expect_eq(marker, 7, "default value parameter is not inferred as fixture");
}

} // namespace fixtures

namespace fixtures {

void free_default_ptr_passthrough(PtrFixture *fx) {
    gentest::expect(fx == nullptr, "defaulted fixture-like pointer parameter is passed through (not fixture-inferred)");
}

} // namespace fixtures

namespace fixtures {

// NOLINTNEXTLINE(performance-unnecessary-value-param)
void free_default_shared_ptr_alias_passthrough(PtrFixtureSharedAlias fx) {
    gentest::expect(!fx, "defaulted shared_ptr fixture-like alias parameter is passed through (not fixture-inferred)");
}

} // namespace fixtures

namespace fixtures {

void free_pointer(PtrFixture *fx) {
    gentest::expect(fx != nullptr, "fixture pointer is valid");
    gentest::expect_eq(fx->value, 3, "fixture state available");
    gentest::expect(PtrFixture::allocations >= 1, "allocation hook runs for pointer fixture");
    gentest::expect_eq(PtrFixture::seen_suite, "", "suite-aware allocation hook gets empty suite for local fixture");
}

} // namespace fixtures

namespace fixtures {

void free_raw_pointer(RawFixture *fx) {
    gentest::expect(fx != nullptr, "fixture pointer is valid");
    gentest::expect_eq(fx->value, 5, "fixture state available");
    gentest::expect(RawFixture::allocations >= 1, "allocation hook runs for raw pointer fixture");
}

} // namespace fixtures

namespace fixtures {

void free_raw_pointer_polymorphic_a_use(RawPolymorphicFixture *fx) {
    gentest::expect(fx != nullptr, "fixture pointer is valid");
    gentest::expect_eq(fx->value, 6, "polymorphic fixture state available");
    gentest::expect_eq(RawPolymorphicFixture::base_deletes, 0, "base destructor has not run before first test");
    gentest::expect_eq(RawPolymorphicFixture::derived_deletes, 0, "derived destructor has not run before first test");
}

} // namespace fixtures

namespace fixtures {

void free_raw_pointer_polymorphic_b_after_first(RawPolymorphicFixture *fx) {
    static int invocations = 0;
    if (invocations == 0) {
        RawPolymorphicFixture::base_deletes    = 0;
        RawPolymorphicFixture::derived_deletes = 0;
    }
    gentest::expect(fx != nullptr, "fixture pointer is valid");
    gentest::expect_eq(RawPolymorphicFixture::base_deletes, invocations, "base destructor count tracks prior exact invocations");
    gentest::expect_eq(RawPolymorphicFixture::derived_deletes, invocations, "derived destructor count tracks prior exact invocations");
    ++invocations;
}

} // namespace fixtures

namespace fixtures {

// NOLINTNEXTLINE(performance-unnecessary-value-param)
void free_shared_ptr(SharedFixtureHandle fx) {
    gentest::expect(static_cast<bool>(fx), "shared fixture pointer is valid");
    gentest::expect_eq(fx->value, 4, "fixture state available");
    gentest::expect(SharedFixture::allocations >= 1, "allocation hook runs for shared fixture");
}

} // namespace fixtures

namespace fixtures::suite_shared::inner_a {

void set(SharedSuiteFx &fx) {
    if (!SharedSuiteFx::first)
        SharedSuiteFx::first = &fx;
    SharedSuiteFx::saw_test = true;
    gentest::expect_eq(SharedSuiteFx::setups, 1, "suite fixture setUp runs once");
    gentest::expect_eq(SharedSuiteFx::teardowns, 0, "suite fixture tearDown not yet run");
    fx.value = 99;
}

} // namespace fixtures::suite_shared::inner_a

namespace fixtures::suite_shared::inner_b {

void check(SharedSuiteAlias &fx) {
    SharedSuiteFx::saw_test = true;
    gentest::expect_eq(&fx, SharedSuiteFx::first, "suite fixture instance reused across namespaces");
    gentest::expect_eq(SharedSuiteFx::setups, 1, "suite fixture setUp runs once");
    gentest::expect_eq(fx.value, 99, "suite fixture state persists");
}

} // namespace fixtures::suite_shared::inner_b

namespace fixtures::global_shared::inner_a {

void hit(SharedGlobalFx &fx) {
    if (!SharedGlobalFx::first)
        SharedGlobalFx::first = &fx;
    SharedGlobalFx::saw_test = true;
    ++fx.hits;
    gentest::expect_eq(SharedGlobalFx::setups, 1, "global fixture setUp runs once");
}

} // namespace fixtures::global_shared::inner_a

namespace fixtures::global_shared::inner_b {

// NOLINTNEXTLINE(performance-unnecessary-value-param)
void check(SharedGlobalAlias fx) {
    SharedGlobalFx::saw_test = true;
    gentest::expect(static_cast<bool>(fx), "shared pointer provided");
    gentest::expect_eq(fx.get(), SharedGlobalFx::first, "global fixture instance reused");
    gentest::expect_eq(fx->hits, 1, "global fixture state persists");
}

} // namespace fixtures::global_shared::inner_b

namespace fixtures::global_shared::inner_c {

void pointer(SharedGlobalRawAlias fx) {
    SharedGlobalFx::saw_test = true;
    gentest::expect(fx != nullptr, "pointer fixture provided");
    gentest::expect_eq(fx, SharedGlobalFx::first, "pointer refers to shared instance");
}

} // namespace fixtures::global_shared::inner_c

namespace fixtures::mixed_suite {

void mixed_one(LocalMix &local, SuiteMix &suite, GlobalMix &global) {
    if (!SuiteMix::first)
        SuiteMix::first = &suite;
    if (!SuiteMix::initialized) {
        suite.value           = 42;
        SuiteMix::initialized = true;
    }
    gentest::expect_eq(SuiteMix::setups, 1, "suite fixture setUp runs once");
    gentest::expect_eq(&suite, SuiteMix::first, "suite fixture instance reused");
    gentest::expect_eq(suite.value, 42, "suite fixture state persists");

    if (!GlobalMix::first)
        GlobalMix::first = &global;
    if (!GlobalMix::initialized) {
        global.value           = 24;
        GlobalMix::initialized = true;
    }
    gentest::expect_eq(GlobalMix::setups, 1, "global fixture setUp runs once");
    gentest::expect_eq(&global, GlobalMix::first, "global fixture instance reused");
    gentest::expect_eq(global.value, 24, "global fixture state persists");

    gentest::expect_eq(LocalMix::setups, LocalMix::teardowns + 1, "local fixture setup/teardown per test");
    gentest::expect_eq(local.value, 0, "local fixture starts fresh");
    local.value = 7;
}

} // namespace fixtures::mixed_suite

namespace fixtures::mixed_suite {

// NOLINTNEXTLINE(performance-unnecessary-value-param)
void mixed_two(LocalMix &local, int marker, SuiteMixAlias &suite, GlobalMixHandle global) {
    gentest::expect(marker == 9 || marker == 11 || marker == 13, "parameter values bound between fixture args");
    gentest::expect_eq(SuiteMix::setups, 1, "suite fixture setUp runs once");
    gentest::expect_eq(&suite, SuiteMix::first, "suite fixture instance reused");
    gentest::expect_eq(suite.value, 42, "suite fixture state persists");

    gentest::expect(static_cast<bool>(global), "global fixture shared pointer provided");
    gentest::expect_eq(GlobalMix::setups, 1, "global fixture setUp runs once");
    gentest::expect_eq(global.get(), GlobalMix::first, "global fixture instance reused");
    gentest::expect_eq(global->value, 24, "global fixture state persists");

    gentest::expect_eq(LocalMix::setups, LocalMix::teardowns + 1, "local fixture setup/teardown per test");
    gentest::expect_eq(local.value, 0, "local fixture starts fresh");
    local.value = marker;
}

} // namespace fixtures::mixed_suite
