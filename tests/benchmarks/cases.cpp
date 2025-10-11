#include "gentest/attributes.h"

#include <cmath>
#include <string>
#include <vector>

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

} // namespace benchmarks
