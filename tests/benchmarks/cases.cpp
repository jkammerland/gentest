#include "gentest/attributes.h"

#include <atomic>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "gentest/bench_util.h"
#include "gentest/runner.h"

namespace benchmarks {

namespace {
inline void record_bench_issue(std::string_view label, std::string_view issue) {
    std::string msg;
    msg.reserve(label.size() + issue.size() + 3);
    msg.append(label);
    msg.append(": ");
    msg.append(issue);
    gentest::detail::record_bench_error(std::move(msg));
}

template <typename T>
struct BenchFixtureState {
    static inline std::atomic<int> setups{0};
    static inline std::atomic<int> teardowns{0};
    static inline std::atomic<const T*> first{nullptr};

    static void on_setup(std::string_view label) {
        if (setups.fetch_add(1, std::memory_order_relaxed) != 0) {
            record_bench_issue(label, "setup called more than once");
        }
    }

    static void on_teardown(std::string_view label) {
        if (teardowns.fetch_add(1, std::memory_order_relaxed) != 0) {
            record_bench_issue(label, "teardown called more than once");
        }
    }

    static void on_call(std::string_view label, const T* self) {
        const T* seen = first.load(std::memory_order_relaxed);
        if (!seen) {
            first.store(self, std::memory_order_relaxed);
            seen = self;
        }
        if (seen != self) {
            record_bench_issue(label, "fixture instance changed");
        }
        if (setups.load(std::memory_order_relaxed) != 1) {
            record_bench_issue(label, "setup count != 1");
        }
        if (teardowns.load(std::memory_order_relaxed) != 0) {
            record_bench_issue(label, "teardown ran before call");
        }
    }
};
} // namespace

namespace spacing {
struct DummyMutex {
    void lock() noexcept {}
    void unlock() noexcept {}
};

template <typename Mutex>
void lock_guard_small() {
    Mutex         m{};
    std::uint64_t sink = 0;
    for (int i = 0; i < 64; ++i) {
        std::lock_guard<Mutex> lock(m);
        sink += static_cast<std::uint64_t>(i);
    }
    gentest::doNotOptimizeAway(sink);
}
} // namespace spacing

[[using gentest: bench("string/concat_small"), baseline]]
void bench_concat_small() {
    // Minimal work; harness repeats this function many times
    std::string a = "hello";
    std::string b = " ";
    std::string c = "world";
    auto s = a + b + c;
    gentest::doNotOptimizeAway(s);
}

[[using gentest: bench("math/sqrt"), baseline]]
void bench_sqrt() {
    // Compute a sqrt to exercise math pipeline
    double x = 12345.6789;
    gentest::doNotOptimizeAway(x);
    double r = std::sqrt(x);
    gentest::doNotOptimizeAway(r);
}

[[using gentest: jitter("math/sin_jitter")]]
void jitter_sin() {
    double x = 1.2345;
    gentest::doNotOptimizeAway(x);
    double y = std::sin(x);
    gentest::doNotOptimizeAway(y);
}

[[using gentest: jitter("math/sin_approx"), baseline]]
void jitter_sin_approx() {
    double x = 0.5;
    gentest::doNotOptimizeAway(x);
    const double x2 = x * x;
    double y = x - (x2 * x) / 6.0;
    gentest::doNotOptimizeAway(x2);
    gentest::doNotOptimizeAway(y);
}

[[using gentest: jitter("math/cos_jitter")]]
void jitter_cos() {
    double x = 1.2345;
    gentest::doNotOptimizeAway(x);
    double y = std::cos(x);
    gentest::doNotOptimizeAway(y);
}

[[using gentest: jitter("math/tan_jitter")]]
void jitter_tan() {
    double x = 0.5;
    gentest::doNotOptimizeAway(x);
    double y = std::tan(x);
    gentest::doNotOptimizeAway(y);
}

[[using gentest: jitter("math/tanh_jitter")]]
void jitter_tanh() {
    double x = 0.5;
    gentest::doNotOptimizeAway(x);
    double y = std::tanh(x);
    gentest::doNotOptimizeAway(y);
}

template <typename Mutex>
// clang-format off
[[using gentest: bench("spacing/lock_guard_small"), baseline]]
[[using gentest: template(Mutex, benchmarks::spacing::DummyMutex, std::mutex)]]
// clang-format on
// Spacing regression: comment between attributes and declaration.
void bench_lock_guard_small() {
    spacing::lock_guard_small<Mutex>();
}

template <typename T>
// clang-format off
[[using gentest: jitter("spacing/jitter_template_params")]]
[[using gentest: template(T, int, long)]]
[[using gentest: parameters(v, 1, 2)]]
// clang-format on
// Spacing regression: comment between attributes and declaration.
void jitter_template_params(int v) {
    gentest::doNotOptimizeAway(v);
    gentest::doNotOptimizeAway(sizeof(T));
}

// Struct and complex parameterization smoke for benches
namespace demo {
struct Blob { int a; int b; };
inline int work(const Blob& b) { return (b.a * 3) + (b.b * 5); }
}

[[using gentest: bench("struct/process"), baseline, parameters(p, benchmarks::demo::Blob{1,2}, benchmarks::demo::Blob{3,4})]]
void bench_struct_params(demo::Blob p) {
    auto v = demo::work(p);
    gentest::doNotOptimizeAway(v);
}

[[using gentest: bench("complex/mag"), baseline,
  parameters(z, std::complex<double>(1.0, 2.0), std::complex<double>(3.0, 4.0))]]
void bench_complex(std::complex<double> z) {
    auto m = std::norm(z);
    gentest::doNotOptimizeAway(m);
}

struct [[using gentest: fixture(suite)]] NullBenchFixture {
    static std::unique_ptr<NullBenchFixture> gentest_allocate() {
        if (std::getenv("GENTEST_BENCH_NULL_FIXTURE")) return {};
        return std::make_unique<NullBenchFixture>();
    }
};

struct [[using gentest: fixture(suite)]] NullJitterFixture {
    static std::unique_ptr<NullJitterFixture> gentest_allocate() {
        if (std::getenv("GENTEST_JITTER_NULL_FIXTURE")) return {};
        return std::make_unique<NullJitterFixture>();
    }
};

[[using gentest: bench("fixture/null"), baseline]]
void bench_null(NullBenchFixture&) {}

[[using gentest: jitter("fixture/jitter_null")]]
void jitter_null(NullJitterFixture&) {}

struct LocalBenchFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() { BenchFixtureState<LocalBenchFixture>::on_setup("benchmarks/fixture/local"); }
    void tearDown() { BenchFixtureState<LocalBenchFixture>::on_teardown("benchmarks/fixture/local"); }
};

struct LocalJitterFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() { BenchFixtureState<LocalJitterFixture>::on_setup("benchmarks/fixture/local_jitter"); }
    void tearDown() { BenchFixtureState<LocalJitterFixture>::on_teardown("benchmarks/fixture/local_jitter"); }
};

[[using gentest: bench("fixture/local")]]
void bench_local(LocalBenchFixture& fx) {
    BenchFixtureState<LocalBenchFixture>::on_call("benchmarks/fixture/local", &fx);
}

[[using gentest: jitter("fixture/local_jitter")]]
void jitter_local(LocalJitterFixture& fx) {
    BenchFixtureState<LocalJitterFixture>::on_call("benchmarks/fixture/local_jitter", &fx);
}

struct [[using gentest: fixture(suite)]] SuiteBenchFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() { BenchFixtureState<SuiteBenchFixture>::on_setup("benchmarks/fixture/free_suite_global/suite"); }
    void tearDown() { BenchFixtureState<SuiteBenchFixture>::on_teardown("benchmarks/fixture/free_suite_global/suite"); }
};

struct [[using gentest: fixture(global)]] GlobalBenchFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() { BenchFixtureState<GlobalBenchFixture>::on_setup("benchmarks/fixture/free_suite_global/global"); }
    void tearDown() { BenchFixtureState<GlobalBenchFixture>::on_teardown("benchmarks/fixture/free_suite_global/global"); }
};

struct [[using gentest: fixture(suite)]] SuiteJitterFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() { BenchFixtureState<SuiteJitterFixture>::on_setup("benchmarks/fixture/free_suite_global_jitter/suite"); }
    void tearDown() { BenchFixtureState<SuiteJitterFixture>::on_teardown("benchmarks/fixture/free_suite_global_jitter/suite"); }
};

struct [[using gentest: fixture(global)]] GlobalJitterFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() { BenchFixtureState<GlobalJitterFixture>::on_setup("benchmarks/fixture/free_suite_global_jitter/global"); }
    void tearDown() { BenchFixtureState<GlobalJitterFixture>::on_teardown("benchmarks/fixture/free_suite_global_jitter/global"); }
};

[[using gentest: bench("fixture/free_suite_global")]]
void bench_free_suite_global(SuiteBenchFixture& suite_fx, GlobalBenchFixture& global_fx) {
    BenchFixtureState<SuiteBenchFixture>::on_call("benchmarks/fixture/free_suite_global/suite", &suite_fx);
    BenchFixtureState<GlobalBenchFixture>::on_call("benchmarks/fixture/free_suite_global/global", &global_fx);
}

[[using gentest: jitter("fixture/free_suite_global_jitter")]]
void jitter_free_suite_global(SuiteJitterFixture& suite_fx, GlobalJitterFixture& global_fx) {
    BenchFixtureState<SuiteJitterFixture>::on_call("benchmarks/fixture/free_suite_global_jitter/suite", &suite_fx);
    BenchFixtureState<GlobalJitterFixture>::on_call("benchmarks/fixture/free_suite_global_jitter/global", &global_fx);
}

} // namespace benchmarks
