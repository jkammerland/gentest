#pragma once

#include "gentest/bench_util.h"
#include "gentest/fixture.h"
#include "gentest/test.h"
#ifdef RECORDING_WITH_GLAZE
#include "gentest/record_glaze.h"
#endif
#ifdef RECORDING_WITH_CBOR
#include "gentest/record_cbor.h"
#endif

#include <array>
#include <numeric>
#include <string>
#include <vector>

namespace recording {
struct Snapshot {
    std::string      device;
    std::vector<int> samples;
};

struct [[gentest::fixture(global)]] Environment : gentest::FixtureSetup {
    void setUp() override { gentest::record_property("device", "simulator"); }
};

struct Input : gentest::FixtureSetup, gentest::FixtureTearDown {
    Snapshot snapshot{"simulator", {1, 2, 3, 4}};

    void setUp() override {
        gentest::record_property("sample_count", snapshot.samples.size());
        const std::array bytes{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
        gentest::record_data("input", bytes, "application/octet-stream", {.schema = "example.input/v1"});
#ifdef RECORDING_WITH_GLAZE
        gentest::record_json("snapshot", snapshot, {.schema = "example.snapshot/v1"});
#endif
#ifdef RECORDING_WITH_CBOR
        gentest::record_cbor("snapshot", snapshot, {.schema = "example.snapshot/v1"});
#endif
    }
    void tearDown() override { gentest::record_property("teardown_complete", true); }
};

[[using gentest: test("sum"), req("SUM-001")]]
inline void sum(Input &input, Environment &) {
    gentest::expect_eq(std::accumulate(input.snapshot.samples.begin(), input.snapshot.samples.end(), 0), 10);
    gentest::record_property("observed_sum", 10);
}

[[gentest::bench("throughput")]] inline void throughput(Input &input) {
    gentest::clobberMemory();
    auto value = std::accumulate(input.snapshot.samples.begin(), input.snapshot.samples.end(), 0);
    gentest::doNotOptimizeAway(value);
}

[[gentest::jitter("latency")]] inline void latency(Input &input) {
    gentest::clobberMemory();
    auto value = std::accumulate(input.snapshot.samples.begin(), input.snapshot.samples.end(), 0);
    gentest::doNotOptimizeAway(value);
}
} // namespace recording
