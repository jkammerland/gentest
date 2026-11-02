#include "cases.hpp"

#include "gentest/detail/generated_runtime.h"
#include "gentest/detail/runtime_support.h"

namespace fixture_selection {

Counters counts;

gentest::awaitable<void> ZDependency::setUp() {
    co_await gentest::async::yield();
    ++counts.dependency_setup;
    alive = true;
    gentest::expect(gentest::detail::bench_phase() != gentest::detail::BenchPhase::Call, "dependency setup must be outside timing");
}

void ZDependency::tearDown() {
    ++counts.dependency_teardown;
    alive = false;
}

ZDependency::~ZDependency() { counts.destruction_order.push_back(2); }

void ASelected::setUp() {
    ++counts.suite_setup;
    std::string error;
    dependency = gentest::detail::get_shared_fixture_typed<ZDependency>(gentest::detail::SharedFixtureScope::Global, "fixture_selection",
                                                                        "fixture_selection::ZDependency", error);
    gentest::expect(dependency && dependency->alive, error);
    gentest::expect(gentest::detail::bench_phase() != gentest::detail::BenchPhase::Call, "suite setup must be outside timing");
}

void ASelected::tearDown() {
    gentest::expect(dependency && dependency->alive, "dependency must remain available during dependent teardown");
    ++counts.suite_teardown;
}

ASelected::~ASelected() { counts.destruction_order.push_back(1); }

void NestedFailure::setUp() {
    gentest::detail::record_bench_error("parent setup error must survive dependency activation");
    ASelected::setUp();
}

Unrelated::Unrelated() { ++counts.unrelated_allocations; }

void Unrelated::setUp() {
    ++counts.unrelated_setup;
    gentest::fail("unrelated database setup must not run");
}

void Local::setUp() { ++counts.local_setup; }
void Local::tearDown() { ++counts.local_teardown; }

void plain() { ++counts.bodies; }

void selected(ASelected &fixture, Local &) {
    gentest::expect(fixture.dependency && fixture.dependency->alive, "selected fixture dependency is ready");
    ++counts.bodies;
}

gentest::awaitable<void> selected_async(ASelected &fixture) {
    co_await gentest::async::yield();
    gentest::expect(fixture.dependency && fixture.dependency->alive, "async case keeps shared fixtures alive");
    ++counts.bodies;
}

void static_skip(Unrelated &) { gentest::fail("static skipped body must not run"); }
void runtime_skip(ASelected &) { gentest::skip("runtime skip after shared fixture acquisition"); }
void unrelated(Unrelated &) { gentest::fail("failed fixture body must not run"); }
void nested_failure(NestedFailure &) { ++counts.bodies; }

void measured(ASelected &fixture) {
    gentest::expect(fixture.dependency && fixture.dependency->alive, "benchmark dependency is ready");
    ++counts.bodies;
}

void latency(ASelected &fixture) {
    gentest::expect(fixture.dependency && fixture.dependency->alive, "jitter dependency is ready");
    ++counts.bodies;
}

void skipped_bench(Unrelated &) { gentest::fail("skipped benchmark must not run"); }
void skipped_jitter(Unrelated &) { gentest::fail("skipped jitter must not run"); }

} // namespace fixture_selection
