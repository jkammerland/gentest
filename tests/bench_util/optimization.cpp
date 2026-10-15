#include "gentest/bench_util.h"

#include <cstdint>

#if defined(_MSC_VER)
#define GENTEST_PROBE_NOINLINE __declspec(noinline)
#else
#define GENTEST_PROBE_NOINLINE __attribute__((noinline))
#endif

namespace {
struct Pair {
    std::uint64_t first;
    std::uint64_t payload;
};

struct ConstMember {
    const std::uint64_t first;
    std::uint64_t       payload;
};

struct Noncopyable {
    explicit Noncopyable(std::uint64_t value) : payload(value) {}
    Noncopyable(const Noncopyable &)            = delete;
    Noncopyable &operator=(const Noncopyable &) = delete;

    std::uint64_t first{7};
    std::uint64_t payload;
};
} // namespace

// Pure, runtime-dependent work: observing byte zero alone cannot retain it in
// either the shifted scalar or the second aggregate member below.
extern "C" GENTEST_PROBE_NOINLINE std::uint64_t gentest_bench_util_mix(std::uint64_t value) noexcept {
    value ^= value >> 33;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 29;
    return value;
}

extern "C" GENTEST_PROBE_NOINLINE void gentest_probe_scalar(std::uint64_t input) {
    auto value = gentest_bench_util_mix(input) << 8;
    gentest::doNotOptimizeAway(value);
}

extern "C" GENTEST_PROBE_NOINLINE void gentest_probe_const_scalar(std::uint64_t input) {
    const auto value = gentest_bench_util_mix(input) << 8;
    gentest::doNotOptimizeAway(value);
}

extern "C" GENTEST_PROBE_NOINLINE void gentest_probe_rvalue_scalar(std::uint64_t input) {
    gentest::doNotOptimizeAway(gentest_bench_util_mix(input) << 8);
}

extern "C" GENTEST_PROBE_NOINLINE void gentest_probe_const_rvalue(std::uint64_t input) {
    const auto value = gentest_bench_util_mix(input) << 8;
    gentest::doNotOptimizeAway(static_cast<const std::uint64_t &&>(value));
}

extern "C" GENTEST_PROBE_NOINLINE void gentest_probe_pair(std::uint64_t input) {
    Pair value{.first = 7, .payload = gentest_bench_util_mix(input)};
    gentest::doNotOptimizeAway(value);
}

extern "C" GENTEST_PROBE_NOINLINE void gentest_probe_const_pair(std::uint64_t input) {
    const Pair value{.first = 7, .payload = gentest_bench_util_mix(input)};
    gentest::doNotOptimizeAway(value);
}

extern "C" GENTEST_PROBE_NOINLINE void gentest_probe_rvalue_pair(std::uint64_t input) {
    gentest::doNotOptimizeAway(Pair{.first = 7, .payload = gentest_bench_util_mix(input)});
}

extern "C" GENTEST_PROBE_NOINLINE void gentest_probe_const_member(std::uint64_t input) {
    ConstMember value{.first = 7, .payload = gentest_bench_util_mix(input)};
    gentest::doNotOptimizeAway(value);
}

extern "C" GENTEST_PROBE_NOINLINE void gentest_probe_array(std::uint64_t input) {
    const std::uint64_t value[]{7, gentest_bench_util_mix(input)};
    gentest::doNotOptimizeAway(value);
}

extern "C" GENTEST_PROBE_NOINLINE void gentest_probe_const_member_array(std::uint64_t input) {
    ConstMember value[]{{.first = 7, .payload = gentest_bench_util_mix(input)}};
    gentest::doNotOptimizeAway(value);
}

extern "C" GENTEST_PROBE_NOINLINE void gentest_probe_noncopyable(std::uint64_t input) {
    Noncopyable value{gentest_bench_util_mix(input)};
    gentest::doNotOptimizeAway(value);
}

// Negative controls prove that this compiler really eliminates unused work.
extern "C" GENTEST_PROBE_NOINLINE void gentest_probe_control_scalar(std::uint64_t input) {
    const auto value = gentest_bench_util_mix(input) << 8;
    static_cast<void>(value);
}

extern "C" GENTEST_PROBE_NOINLINE void gentest_probe_control_pair(std::uint64_t input) {
    const Pair value{.first = 7, .payload = gentest_bench_util_mix(input)};
    static_cast<void>(value);
}
