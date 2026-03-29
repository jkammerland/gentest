# gentest

`gentest` is an attribute-driven C++ test runner plus a clang-tools-based code generator.

Write tests with standard C++ attributes (`[[gentest::...]]` or `[[using gentest: ...]]`). During the build, `gentest_codegen` scans your sources and generates registrations/wrappers from those attributes. This avoids macro-heavy registration and keeps test declarations in normal C++ syntax. The tradeoff is higher tooling complexity, but it enables orthogonal code generation and additional features.

Features include:
- Arbitrary mocking with no extra declarations
- Easy syntax for multi-dimensional parameterized and templated cases
- Support for multiple fixtures per test case
- Sharing fixtures between test cases
- Native syntax for tags, requirements, and custom labels
- Unified APIs for other test kinds (for example, benchmark, jitter, and coroutine cases)

>[!NOTE]
> Start at [`docs/index.md`](docs/index.md) for the rest of the docs.

## Requirements

- CMake ≥ 3.31
- C++20 compiler
- LLVM/Clang (for `gentest_codegen`)

>[!IMPORTANT]
> `gentest_codegen` consumes your build’s `compile_commands.json` (`CMAKE_EXPORT_COMPILE_COMMANDS=ON`).

## Build & run this repo

```bash
cmake --preset=debug-system
cmake --build --preset=debug-system
ctest --preset=debug-system --output-on-failure
```

## Use in your project (CMake)

`cases.cpp`:

```cpp
#include "gentest/attributes.h"
#include "gentest/runner.h"
using namespace gentest::asserts;

namespace demo {

[[gentest::test]]
void basic() { 
    EXPECT_TRUE(1 + 1 == 2); 
}

} // namespace demo
```

`CMakeLists.txt`:

```cmake
include(CTest)
enable_testing()

# Provides `gentest::gentest` / `gentest::gentest_main` and includes GentestCodegen.cmake.
find_package(gentest CONFIG REQUIRED)
#
# Alternative (as a subproject):
# add_subdirectory(path/to/gentest)

add_executable(my_tests cases.cpp)
# Include-based consumers can keep linking the stock main target directly.
target_link_libraries(my_tests PRIVATE gentest::gentest_main)

# If your tests use `import gentest;` / `import gentest.mock;`, link the public
# module carrier target plus either gentest_main (stock main) or gentest_runtime
# (custom main).
# target_link_libraries(my_tests PRIVATE gentest::gentest gentest::gentest_main)
# target_link_libraries(my_tests PRIVATE gentest::gentest gentest::gentest_runtime)
#
# NOTE: This mode requires a single-config generator/build dir.
gentest_attach_codegen(my_tests)
# Optional: pass extra clang args to the generator (e.g. `-resource-dir ...`) via
# `gentest_attach_codegen(... CLANG_ARGS ...)` or override
# `GENTEST_CODEGEN_DEFAULT_CLANG_ARGS`.
#
# Multi-config generators (Ninja Multi-Config / VS / Xcode):
# gentest_attach_codegen(my_tests OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/my_tests_gentest.cpp")

# Register each discovered case as its own CTest test (like gtest/catch/doctest).
gentest_discover_tests(my_tests)
# Death tests (tagged `death`) are auto-registered as CTest tests with a `death/` prefix.
# Optional: EXPECT_SUBSTRING enforces a substring in the death harness output
# (for normal tests, use PASS_REGULAR_EXPRESSION via PROPERTIES).
# See linked docs below for death-test details and discover options.
# gentest_discover_tests(my_tests EXPECT_SUBSTRING "fatal path")
#
# More gentest_attach_codegen options:
#  OUTPUT / OUTPUT_DIR / SOURCES / DEPENDS / ENTRY / NO_INCLUDE_SOURCES / STRICT_FIXTURE / QUIET_CLANG
# Cross-builds: set GENTEST_CODEGEN_EXECUTABLE or GENTEST_CODEGEN_TARGET before gentest_attach_codegen().
#
# Host-toolchain / sysroot guidance:
#   docs/buildsystems/host_toolchain_sysroots.md

# Alternative: run everything in a single process.
# add_test(NAME my_tests COMMAND my_tests)
```

Docs: [Modules guide](docs/modules.md), [Codegen compiler selection](docs/codegen_compiler_selection.md), [Death tests](docs/death_tests.md), [CTest discovery options](docs/discover_tests.md).

Run:

```bash
./my_tests --list-tests
./my_tests --list
./my_tests --list-death
./my_tests --run=<exact-name>
./my_tests --filter=unit/* --kind=test
./my_tests --include-death --run=death/fatal_path
./my_tests --fail-fast --repeat=2
./my_tests --shuffle --seed 123
./my_tests --no-color
./my_tests --github-annotations
./my_tests
```

`--list-tests` prints only resolved test names (one per line).
`--list` prints the richer listing format (name plus metadata such as tags/owner when present).
`--kind` restricts execution/filtering to `all|test|bench|jitter` (default `all`).
Examples below use the concise `[[gentest::...]]` spelling for single attributes and `[[using gentest: ...]]` for multi-attribute lists.

Naming:
- Any gentest function-level attribute marks the declaration as a case.
- `test("...")` is optional; if omitted, the base name defaults to the C++ function name.
- Member tests are a legacy path and should be avoided in new code.
- Use `test("...")` to disambiguate overloads and keep names stable across refactors.

Tags/metadata:
- Flag attributes are collected as tags: `fast`, `slow`, `linux`, `windows`, `death`.
- Value attributes attach metadata: `req("BUG-123")`, `owner("team-runtime")`, `skip("reason")`.
- `req("...")` is the requirement-to-test mapping hook for traceability workflows (see [docs/traceability_standards.md](docs/traceability_standards.md)).
- Requirement IDs are shown in `--list` output as `requires=...` and exported in JUnit as
  `<property name="requirement" value="...">`, so you can build a trace matrix from CI artifacts (example flow: [docs/traceability_standards.md](docs/traceability_standards.md)).
- `death`-tagged tests are excluded from the default run; pass `--include-death` to execute them.
- The death-test harness treats non-zero exit as success (and fails on normal test failures); set `EXPECT_SUBSTRING` to assert output.
- Full death-test docs: [docs/death_tests.md](docs/death_tests.md).

## Feature examples

### Assertions, failures, and exceptions

`gentest` provides lightweight assertion helpers in `gentest::asserts` (inline functions, not macros):

```cpp
#include "gentest/attributes.h"
#include "gentest/runner.h"
using namespace gentest::asserts;

namespace math {

[[gentest::test]]
void add() {
    EXPECT_EQ(1 + 1, 2);
    ASSERT_TRUE(2 + 2 == 4, "fatal: aborts the current test");
}

} // namespace math
```

Exceptions:
- If a test throws a `std::exception`, the runner records a failure like `unexpected std::exception: ...` and continues.
- Include-based consumers can use the gtest-like macros.
- `import gentest;` consumers should use the matching function templates from `gentest::asserts`.

```cpp
#include "gentest/attributes.h"
#include "gentest/runner.h"
using namespace gentest::asserts;

#include <stdexcept>

[[gentest::test("exceptions/macros")]]
void macros() {
    EXPECT_THROW(throw std::runtime_error("boom"), std::runtime_error);
    EXPECT_THROW(throw 123, int);
    EXPECT_NO_THROW((void)0);
}
```

```cpp
#include <stdexcept>
import gentest;
using namespace gentest::asserts;

[[gentest::test("exceptions/module_functions")]]
void module_functions() {
    EXPECT_THROW<std::runtime_error>([] { throw std::runtime_error("boom"); });
    EXPECT_NO_THROW([] {});
}
```

- For more detailed checks (e.g. matching `what()`), use normal C++ `try`/`catch` and `gentest::fail(...)`:

```cpp
#include "gentest/attributes.h"
#include "gentest/runner.h"

#include <exception>
#include <string_view>

[[gentest::test("exceptions/handled")]]
void handled() {
    try {
        might_throw();
        gentest::fail("expected might_throw() to throw");
    } catch (const std::exception& e) {
        gentest::expect(std::string_view(e.what()).find("boom") != std::string_view::npos, "message contains 'boom'");
    }
}
```

### DEATH / termination (including no-exceptions builds)

With exceptions enabled (the common case), `ASSERT_*` aborts only the *current test* by throwing `gentest::assertion`
(not derived from `std::exception`); the runner catches it and continues.

With exceptions disabled (e.g. `-fno-exceptions`), fatal operations terminate the process (via `std::terminate()`):
- `ASSERT_*` / `gentest::require*`
- `gentest::skip(...)`

Build hint (GCC/Clang):

```cmake
target_compile_options(my_tests PRIVATE -fno-exceptions)
target_compile_definitions(my_tests PRIVATE FMT_EXCEPTIONS=0) # and on MSVC STL: _HAS_EXCEPTIONS=0
```

To test these “death” paths, tag them and run them in their own process:

```cpp
[[using gentest: test("death/fatal_path"), death]]
void fatal_path();
```

```cmake
gentest_discover_tests(my_tests
  # Optional: enforce expected output substring for death tests
  # EXPECT_SUBSTRING "fatal path"
)
```

Manual run:

```bash
./my_tests --run=death/fatal_path --include-death
```

Note: if a death test is compiled out in a configuration (e.g. wrapped in `#ifndef NDEBUG`), it won't
appear in `--list-death`, so no CTest entry is created for that config.

### Outcomes (skip / xfail)

`skip(...)` marks a test skipped and stops executing it. `xfail(...)` marks an expected failure: if the test fails or
throws, it’s reported as XFAIL; if it passes, it’s reported as XPASS (failure).

```cpp
#include "gentest/attributes.h"
#include "gentest/runner.h"
using namespace gentest::asserts;

[[gentest::test("outcomes/skip")]]
void skip_example() {
    gentest::skip("not supported on this configuration");
}

[[gentest::test("outcomes/xfail")]]
void xfail_example() {
    gentest::xfail("BUG-123: known issue");
    EXPECT_EQ(1, 2, "expected to fail");
}
```

### Threads/coroutines + logging (ctx::Adopt)

Assertions must run under an active test context. When you spawn threads/coroutines, adopt the current context so
`EXPECT_*` failures are attributed to the right test. Use `gentest::log_on_fail(true)` + `gentest::log(...)` for
lightweight attachments.

```cpp
#include "gentest/attributes.h"
#include "gentest/runner.h"
using namespace gentest::asserts;

#include <thread>

[[gentest::test("concurrency/adopt_and_log")]]
void adopt_and_log() {
    gentest::log_on_fail(true);
    auto tok = gentest::ctx::current();

    std::thread t([tok] {
        gentest::ctx::Adopt adopt(tok);
        gentest::log("from child thread");
        EXPECT_EQ(1, 2, "failure recorded on parent test");
    });
    t.join();
}
```

Completion semantics are strict by design: runner phase completion waits until all adopted contexts are released.

>[!WARNING]
> If adopted work is detached or stuck and never releases `gentest::ctx::Adopt`, the test/run blocks forever.

### Parameters (value matrices)

Use named parameter axes to generate a Cartesian product of value sets:

```cpp
#include "gentest/attributes.h"
#include "gentest/runner.h"

[[gentest::test("params/pairs")]]
[[gentest::parameters(a, 1, 2)]]
[[gentest::parameters(b, 10, 20)]]
void pairs(int a, int b) {
    gentest::expect((a == 1 || a == 2) && (b == 10 || b == 20), "values from axes");
}

// “Row” style instead of a Cartesian product:
[[using gentest: test("params/rows"), parameters_pack((a, b), (1, 10), (2, 20))]]
void rows(int a, int b) {
    gentest::expect((a == 1 && b == 10) || (a == 2 && b == 20), "row values");
}
```

The generator also supports convenience axes like `range(...)`, `linspace(...)`, `geom(...)`, and `logspace(...)` (see
`include/gentest/attributes.h`).

### Templates (type + non-type matrices)

Generate statically-typed matrices for function templates:

```cpp
#include "gentest/attributes.h"
#include "gentest/runner.h"

template <typename T, int N>
[[using gentest: test("templates/matrix"), template(T, int, long), template(N, 1, 2)]]
void matrix() {
    gentest::expect(true, "instantiated");
}
```

### Fixtures

Fixture arguments are inferred from the free-function signature. If a fixture implements
`gentest::FixtureSetup`/`gentest::FixtureTearDown`, hooks run automatically.
Prefer free-function tests/benches/jitters; member tests are legacy.

Supported fixture argument forms:
- `T&` (reference to the managed fixture instance)
- `T*` (pointer to the managed fixture instance)
- `std::shared_ptr<T>`

Allocation hooks can return `std::unique_ptr<T>` (or custom-deleter unique ptr),
`std::shared_ptr<T>`, or raw `T*`. Gentest always manages fixture ownership
internally. Raw-pointer returns are adopted into managed ownership (same lifetime
semantics as returning `std::unique_ptr<T>`), and the framework handles
deallocation.

`std::unique_ptr<T>` is an allocation-hook return form, not a test-parameter
form.

>[!NOTE]
>This example's teardown assertions assume the three cases below run together:
>`fx/local/one`, `fx/shared/first`, and `fx/shared/second`. If you run only a
>subset, the shared-fixture touch-count checks will fail by design.

```cpp
#include "gentest/attributes.h"
#include "gentest/fixture.h"
#include "gentest/runner.h"

using namespace gentest::asserts;

namespace fx {

struct CounterBase : gentest::FixtureSetup, gentest::FixtureTearDown {
    int set_up_calls = 0;
    int touches      = 0;

    void setUp() override { ++set_up_calls; }

    void touch() { ++touches; }

    void tearDown() override {
        EXPECT_EQ(set_up_calls, 1, "fixture setup runs once");
        EXPECT_EQ(touches, 1, "local fixture is touched once");
    }
};

template <int ExpectedTouches>
struct SharedCounterBase : CounterBase {
    void tearDown() override {
        EXPECT_EQ(this->set_up_calls, 1, "shared fixture setup runs once");
        EXPECT_EQ(this->touches, ExpectedTouches, "shared fixture collected all touches");
    }
};

// Declare the global fixture in the common ancestor namespace so it is visible
// to both `fx::local` and `fx::shared`.
struct [[gentest::fixture(global)]] GlobalCounter : SharedCounterBase<3> {};

namespace local {

struct LocalCounter : CounterBase {};

[[gentest::test]]
void one(LocalCounter& local_fx, GlobalCounter& global_fx) {
    local_fx.touch();
    global_fx.touch();
}

} // namespace local

namespace shared {

struct [[gentest::fixture(suite)]] SuiteCounter : SharedCounterBase<2> {};

[[gentest::test]]
void first(SuiteCounter& suite_fx, GlobalCounter& global_fx) {
    suite_fx.touch();
    global_fx.touch();
}

[[gentest::test]]
void second(SuiteCounter& suite_fx, GlobalCounter& global_fx) {
    suite_fx.touch();
    global_fx.touch();
}

} // namespace shared

} // namespace fx
```

See [docs/fixtures_allocation.md](docs/fixtures_allocation.md) for the full allocation and ownership model.

### Mocks

`gentest::mock<T>` now comes from an explicit mock target. You declare mock defs once with
`gentest_add_mocks(...)`, then your tests include or import the generated mock surface and link the mock target.

Header-defined mock example:

```cmake
gentest_add_mocks(clock_mocks
  DEFS ${CMAKE_CURRENT_SOURCE_DIR}/clock_mocks.hpp
  OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/mocks"
  HEADER_NAME public/clock_mocks.hpp)

add_executable(clock_tests cases.cpp)
target_link_libraries(clock_tests PRIVATE gentest::gentest_main clock_mocks)
gentest_attach_codegen(clock_tests)
gentest_discover_tests(clock_tests)
```

Mock defs (`clock_mocks.hpp`):

```cpp
#pragma once

#include "clock.h"

namespace mytests::mocks {
using ClockMock = gentest::mock<Clock>;
}
```

Test (`cases.cpp`):

```cpp
#include "gentest/attributes.h"
#include "public/clock_mocks.hpp"
using namespace gentest::asserts;

int read_now(const Clock* c) { return c->now(); }

[[using gentest: test("mock/clock")]]
void mock_clock() {
    mytests::mocks::ClockMock clock;
    gentest::expect(clock, &Clock::now).times(1).returns(123);
    EXPECT_EQ(read_now(&clock), 123);
}
```

After the generated surface is visible, raw `gentest::mock<T>` is also valid:

```cpp
gentest::mock<Clock> raw_clock;
gentest::expect(raw_clock, &Clock::now).times(1).returns(456);
```

Named-module mock usage is the same idea, but the public surface is a generated module:

```cmake
gentest_add_mocks(service_mocks
  DEFS
    ${CMAKE_CURRENT_SOURCE_DIR}/service.cppm
    ${CMAKE_CURRENT_SOURCE_DIR}/mock_defs.cppm
  OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/mocks"
  MODULE_NAME mytests.service_mocks)

add_executable(module_tests
  main.cpp
  cases.cppm)
target_link_libraries(module_tests PRIVATE gentest::gentest_runtime service_mocks)
gentest_attach_codegen(module_tests)
gentest_discover_tests(module_tests)
```

Link explicit mock targets before `gentest_attach_codegen()`, so codegen sees the generated mock surface during the first parse.

Mock defs module (`mock_defs.cppm`):

```cpp
export module mytests.mock_defs;

import gentest.mock;

export import mytests.service;

export namespace mytests::mocks {
using ServiceMock = gentest::mock<mytests::Service>;
}
```

Consumer module (`cases.cppm`):

```cpp
export module mytests.cases;

import gentest;
import mytests.service_mocks;

[[using gentest: test("module/mock")]]
void module_mock() {
    mytests::mocks::ServiceMock mock_service;
    gentest::expect(mock_service, &mytests::Service::compute).times(1).with(3).returns(9);
}
```

Safeguards:
- Mocked target definitions may live in headers, header-like files, or named modules. Ordinary source files are still rejected by codegen.
- Header-like files with nonstandard extensions (for example `.mpp`) are accepted when treated as headers (not as named module interfaces).
- Generated mock-registry includes are relative when possible and fall back to absolute paths for cross-root/cross-drive headers (Windows-only path constraint).

Generated mock outputs are partitioned by visibility domain. A single target can mix classic/header-defined mocks with mocks defined in multiple named modules; classic TUs pick up the header-domain shard, and each named module source picks up only its own generated module-domain shard.

Header implementation to mock (`sink.h`):

```cpp
#pragma once

struct Sink {
    void write(int value) { last = value; }
    int last = 0;
};
```

Test file (`cases.cpp`):

Assume `public/sink_mocks.hpp` is the generated header surface from an explicit mock target, for example one that publishes `demo::mocks::SinkMock` and `demo::mocks::TickerMock`.

```cpp
#include "gentest/attributes.h"
#include "public/sink_mocks.hpp"
using namespace gentest::asserts;

template <class SinkLike>
void emit(SinkLike* s) {
    s->write(7);
}

[[gentest::test("mock/nonvirtual")]]
void mock_nonvirtual() {
    demo::mocks::SinkMock sink;
    EXPECT_CALL(sink, write).times(1).with(7);
    emit(&sink);
}
```

Matchers (`.where(...)` with `gentest::match` helpers):

```cpp
#include "public/sink_mocks.hpp"
using namespace gentest::match;

demo::mocks::SinkMock sink;
EXPECT_CALL(sink, write).times(2).where(InRange(10, 20));
Sink* sink_ptr = &sink;
sink_ptr->write(12);
```

Static member functions can be mocked too. Example type:

```cpp
struct Ticker {
    static int add(int lhs, int rhs) noexcept { return lhs + rhs; }
};

demo::mocks::TickerMock t;
EXPECT_CALL(t, add).times(1).returns(123);
EXPECT_EQ(t.add(4, 5), 123);
```

Use `t.add(...)` (through the mock object). A direct `Ticker::add(...)` call bypasses the mock.

### Benchmarks and jitter

Define microbenchmarks and jitter benchmarks (for timing variance):

```cpp
#include "gentest/attributes.h"
#include "gentest/bench_util.h"

#include <cmath>
#include <string>

[[gentest::bench("bench/concat")]]
void bench_concat() {
    std::string s = "hello";
    s += " world";
    gentest::doNotOptimizeAway(s);
}

[[gentest::jitter("bench/sin")]]
void jitter_sin() {
    volatile double x = 1.2345;
    gentest::doNotOptimizeAway(std::sin(x));
}
```

CLI:

```bash
./my_tests --list-benches
./my_tests --run=bench/concat --kind=bench
./my_tests --filter=bench/* --kind=bench --bench-table
./my_tests --bench-min-epoch-time-s=0.02 --bench-epochs=8 --bench-warmup=2 --bench-max-total-time-s=5
./my_tests --run=bench/sin --kind=jitter --jitter-bins=20
./my_tests --filter=bench/* --kind=jitter --jitter-bins=20
./my_tests --filter=bench/* --kind=all --time-unit=ns
```

Bench/jitter execution is phase-based per measured case:
- `setup`: runs once before calibration/warmup/measurement call loops.
- `call`: runs in loops; reported benchmark/jitter metrics come from this phase.
- `teardown`: runs once after call loops.
Failures are reported by phase (`setup`, `call`, or `teardown`), and the executable exits non-zero.

### Reporting (JUnit / Allure / GitHub annotations)

```bash
./my_tests --junit=./junit.xml
./my_tests --allure-dir=./allure-results
./my_tests --github-annotations
```

`--allure-dir` currently requires a `GENTEST_USE_BOOST_JSON=ON` build. Without that backend, no Allure files are written. In supported builds, measured cases emit richer native artifacts:
- bench metrics as TSV plus an SVG summary plot
- jitter metrics/histogram as TSV plus an SVG histogram plot and sampled raw JSON

Minimal CDash dashboard support is available through `ctest -S`:

```bash
ctest -S cmake/cdash/Experimental.cmake -VV
GENTEST_CDASH_ENABLE_ALLURE_TESTS=ON ctest -S cmake/cdash/Experimental.cmake -VV
GENTEST_CDASH_SUBMIT_URL='https://cdash.example/submit.php?project=gentest' ctest -S cmake/cdash/Experimental.cmake -VV
```

Useful overrides are read from the environment:
- `GENTEST_CDASH_SOURCE_DIR`
- `GENTEST_CDASH_BINARY_DIR`
- `GENTEST_CDASH_GENERATOR`
- `GENTEST_CDASH_PARALLEL_LEVEL`
- `GENTEST_CDASH_CONFIGURATION`
- `GENTEST_CDASH_ENABLE_ALLURE_TESTS`
- `GENTEST_CDASH_BUILD_TARGETS`
- `GENTEST_CDASH_TEST_REGEX`
- `GENTEST_CDASH_SUBMIT_URL`
- `GENTEST_CDASH_DRY_RUN=ON`

>[!WARNING]
> Cross-compiling requires a *host* `gentest_codegen` executable. See
> [`docs/cross_compile/linux_aarch64_qemu.md`](docs/cross_compile/linux_aarch64_qemu.md) and the install templates under
> [`docs/install/`](docs/install/).

## Docs

- Docs index: [`docs/index.md`](docs/index.md)
- Install templates: [`Linux`](docs/install/linux.md), [`macOS`](docs/install/macos.md), [`Windows`](docs/install/windows.md)
- Repo-local buildsystem guides: [`Meson`](docs/buildsystems/meson.md), [`Xmake`](docs/buildsystems/xmake.md), [`Bazel`](docs/buildsystems/bazel.md)
