#include "cases.hpp"

namespace benchmarks {

void bench_struct_params(demo::Blob p) {
    auto v = demo::work(p);
    gentest::doNotOptimizeAway(v);
}

} // namespace benchmarks

namespace benchmarks {

void bench_concat_small() {
    // Minimal work; harness repeats this function many times
    std::string a = "hello";
    std::string b = " ";
    std::string c = "world";
    auto        s = a + b + c;
    gentest::doNotOptimizeAway(s);
}

} // namespace benchmarks

namespace benchmarks {

void bench_csv_comma_name() {
    int value = 42;
    gentest::doNotOptimizeAway(value);
}

} // namespace benchmarks

namespace benchmarks {

void bench_sqrt() {
    // Compute a sqrt to exercise math pipeline
    double x = 12345.6789;
    gentest::doNotOptimizeAway(x);
    double r = std::sqrt(x);
    gentest::doNotOptimizeAway(r);
}

} // namespace benchmarks

namespace benchmarks {

void jitter_sin() {
    double x = 1.2345;
    gentest::doNotOptimizeAway(x);
    double y = std::sin(x);
    gentest::doNotOptimizeAway(y);
}

} // namespace benchmarks

namespace benchmarks {

void jitter_sin_approx() {
    double x = 0.5;
    gentest::doNotOptimizeAway(x);
    const double x2 = x * x;
    double       y  = x - (x2 * x) / 6.0;
    gentest::doNotOptimizeAway(x2);
    gentest::doNotOptimizeAway(y);
}

} // namespace benchmarks

namespace benchmarks {

void jitter_cos() {
    double x = 1.2345;
    gentest::doNotOptimizeAway(x);
    double y = std::cos(x);
    gentest::doNotOptimizeAway(y);
}

} // namespace benchmarks

namespace benchmarks {

void jitter_tan() {
    double x = 0.5;
    gentest::doNotOptimizeAway(x);
    double y = std::tan(x);
    gentest::doNotOptimizeAway(y);
}

} // namespace benchmarks

namespace benchmarks {

void jitter_tanh() {
    double x = 0.5;
    gentest::doNotOptimizeAway(x);
    double y = std::tanh(x);
    gentest::doNotOptimizeAway(y);
}

} // namespace benchmarks

namespace benchmarks {

void bench_complex(std::complex<double> z) {
    auto m = std::norm(z);
    gentest::doNotOptimizeAway(m);
}

} // namespace benchmarks

namespace benchmarks {

void bench_null(NullBenchFixture &) {}

} // namespace benchmarks

namespace benchmarks {

void jitter_null(NullJitterFixture &) {}

} // namespace benchmarks

namespace benchmarks {

void bench_local(LocalBenchFixture &fx) { BenchFixtureState<LocalBenchFixture>::on_call("benchmarks/fixture/local", &fx); }

} // namespace benchmarks

namespace benchmarks {

void jitter_local(LocalJitterFixture &fx) { BenchFixtureState<LocalJitterFixture>::on_call("benchmarks/fixture/local_jitter", &fx); }

} // namespace benchmarks

namespace benchmarks {

void bench_free_suite_global(SuiteBenchFixture &suite_fx, GlobalBenchFixture &global_fx) {
    BenchFixtureState<SuiteBenchFixture>::on_call("benchmarks/fixture/free_suite_global/suite", &suite_fx);
    BenchFixtureState<GlobalBenchFixture>::on_call("benchmarks/fixture/free_suite_global/global", &global_fx);
}

} // namespace benchmarks

namespace benchmarks {

void jitter_free_suite_global(SuiteJitterFixture &suite_fx, GlobalJitterFixture &global_fx) {
    BenchFixtureState<SuiteJitterFixture>::on_call("benchmarks/fixture/free_suite_global_jitter/suite", &suite_fx);
    BenchFixtureState<GlobalJitterFixture>::on_call("benchmarks/fixture/free_suite_global_jitter/global", &global_fx);
}

} // namespace benchmarks
