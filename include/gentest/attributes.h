#pragma once

// Annotation guidance:
//
// Use standard C++11 attributes with a vendor namespace to tag tests and
// attach metadata:
//
//   [[using gentest : test("suite/case"), group("core"), req("BUG-123"), slow, linux]]
//   void my_test();
//
// Benchmarks:
//   [[using gentest : bench("suite/bench_name")]]
//   void my_benchmark();
//   - Run via --run-bench= or --bench-filter=, list via --list-benches.
//   - Optional flags: --bench-min-epoch-time-s, --bench-epochs, --bench-warmup, --bench-max-total-time-s
//
// Fixture composition for free functions:
//   [[using gentest : test("suite/free"), fixtures(A, B, C)]]
//   void my_free_test(A& a, B& b, C& c);
//   - A/B/C are default-constructed per invocation and passed by reference.
//   - If a fixture derives from gentest::FixtureSetup/TearDown, setUp/tearDown are called automatically.
//   - Applies to free functions only (not member tests) and is always ephemeral.
//
// The `test("...")` attribute is required and defines the public case name.
// Additional attribute names (e.g. `group("name")`, `slow`, `linux`) are collected as tags,
// while attributes such as `req("BUG-123")` or `skip("reason")` attach
// requirements or skipping instructions. All information is extracted by the
// code generator—no macros or compiler-specific annotations required.

// Benchmarking:
//   [[using gentest : bench("suite/name")]]
//   [[using gentest : jitter("suite/name")]]
//
// Parameter generators (global convenience):
//   [[using gentest : range(i, 1, 2, 9)]]                 // 1,3,5,7,9 (integers)
//   [[using gentest : range(i, "1:2:9")]]                 // Matlab-style triple in a string
//   [[using gentest : linspace(x, 0.0, 1.0, 5)]]           // 0.0, 0.25, 0.5, 0.75, 1.0
//   [[using gentest : geom(n, 1, 2, 5)]]                   // 1,2,4,8,16 (geom progression)
//   [[using gentest : logspace(f, -3, 3, 7)]]              // 1e-3 .. 1e+3 (base 10)

// This header intentionally declares no symbols; it documents the attribute
// format consumed by the generator and serves as a stable include for tests.
