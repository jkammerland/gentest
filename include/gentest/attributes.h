#pragma once

// Annotation guidance:
//
// Use standard C++11 attributes with a vendor namespace to tag tests and
// attach metadata:
//
//   [[using gentest : test("suite/case"), req("BUG-123"), slow, linux]]
//   void my_test();
//
// Benchmarks:
//   [[using gentest : bench("suite/bench_name")]]
//   void my_benchmark();
//   - Run via --run-bench= or --bench-filter=, list via --list-benches.
//   - Optional flags: --bench-table, --bench-min-epoch-time-s, --bench-epochs, --bench-warmup, --bench-max-total-time-s
//   - Jitter runs via --run-jitter= or --jitter-filter=; use --jitter-bins to control histogram bins.
//
// Fixture composition for test/bench/jitter function parameters:
//   [[using gentest : test("suite/free")]]
//   void my_free_test(A& a, B& b, C& c);
//   - Any parameter not listed by `parameters(...)` / `parameters_pack(...)` /
//     range/linspace/geom/logspace is treated as a fixture argument.
//   - Trailing parameters with C++ default arguments are passed through as
//     normal defaulted values (not fixture-inferred).
//   - Unannotated fixture types are local (per invocation).
//   - Types marked `[[using gentest: fixture(suite)]]` / `fixture(global)` are shared.
//   - If a fixture derives from gentest::FixtureSetup/TearDown, setUp/tearDown are called automatically.
//   - The legacy `fixtures(...)` attribute is removed and now rejected.
//
// Naming:
// - Any gentest function-level attribute marks the declaration as a case.
// - `test("...")` is optional and overrides the base name. If omitted, the base
//   name falls back to the C++ function name (or `FixtureType/method` for member
//   tests).
// - Use `test("...")` to disambiguate overloads and keep names stable across
//   refactors.
// - `baseline` is only valid for `bench`/`jitter` cases.
// Additional attribute names (e.g. `slow`, `linux`) are collected as tags,
// while attributes such as `req("BUG-123")` or `skip("reason")` attach
// requirements or skipping instructions. All information is extracted by the
// code generator—no macros or compiler-specific annotations required.
//
// Death tests:
//   [[using gentest : test("suite/crash_on_x"), death]]
//   - Tagged tests are excluded from the default "run all" execution to avoid
//     aborting the full test run.
//   - Run explicitly via --run-test=... with --include-death, or wire them into
//     CTest using a death-test harness (see GentestCodegen.cmake helpers).

// Benchmarking:
//   [[using gentest : bench("suite/name")]]
//   [[using gentest : jitter("suite/name")]]
//   [[using gentest : baseline]] // optional: marks a bench as baseline within the suite
//
// Parameter generators (global convenience):
//   [[using gentest : range(i, 1, 2, 9)]]                 // 1,3,5,7,9 (integers)
//   [[using gentest : range(i, "1:2:9")]]                 // Matlab-style triple in a string
//   [[using gentest : linspace(x, 0.0, 1.0, 5)]]           // 0.0, 0.25, 0.5, 0.75, 1.0
//   [[using gentest : geom(n, 1, 2, 5)]]                   // 1,2,4,8,16 (geom progression)
//   [[using gentest : logspace(f, -3, 3, 7)]]              // 1e-3 .. 1e+3 (base 10)

// This header intentionally declares no symbols; it documents the attribute
// format consumed by the generator and serves as a stable include for tests.
