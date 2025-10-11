#include "gentest/attributes.h"

#include <cmath>
#include <string>
#include <vector>
#include <complex>
#include "gentest/bench_util.h"

namespace [[using gentest: suite("benchmarks")]] benchmarks {

[[using gentest: bench("string/concat_small")]]
void bench_concat_small() {
    // Minimal work; harness repeats this function many times
    std::string a = "hello";
    std::string b = " ";
    std::string c = "world";
    volatile auto s = a + b + c; // prevent optimizing fully away
    (void)s;
}

[[using gentest: bench("math/sqrt")]]
void bench_sqrt() {
    // Compute a sqrt to exercise math pipeline
    volatile double x = 12345.6789;
    volatile double r = std::sqrt(x);
    (void)r;
}

[[using gentest: jitter("math/sin_jitter")]]
void jitter_sin() {
    volatile double x = 1.2345;
    volatile double y = std::sin(x);
    (void)y;
}

// Struct and complex parameterization smoke for benches
namespace demo {
struct Blob { int a; int b; };
inline int work(const Blob& b) { return (b.a * 3) + (b.b * 5); }
}

[[using gentest: bench("struct/process"), parameters(p, demo::Blob{1,2}, demo::Blob{3,4})]]
void bench_struct_params(demo::Blob p) {
    auto v = demo::work(p);
    gentest::doNotOptimizeAway(v);
}

[[using gentest: bench("complex/mag"), parameters(z, std::complex<double>(1.0, 2.0), std::complex<double>(3.0, 4.0))]]
void bench_complex(std::complex<double> z) {
    auto m = std::norm(z);
    gentest::doNotOptimizeAway(m);
}

} // namespace benchmarks
