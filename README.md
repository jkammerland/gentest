# gentest

`gentest` is an attribute-driven C++ test runner plus a clang-tools-based code generator.

Write tests with standard C++ attributes (`[[using gentest: ...]]`). During the build, `gentest_codegen` scans your sources and generates registrations/wrappers from those attributes. This avoids macro-heavy registration and keeps test declarations in normal C++ syntax. The tradeoff is higher tooling complexity.

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

[[using gentest: test]]
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
target_link_libraries(my_tests PRIVATE gentest::gentest_main)

# Default (recommended): per-TU registration (gtest/catch/doctest-like).
# NOTE: This mode requires a single-config generator/build dir (e.g. Ninja).
gentest_attach_codegen(my_tests)
# Per-TU mode enforces case-insensitive uniqueness for generated TU headers.
# If two sources map to the same header name ignoring case, codegen fails fast.
# Per-TU mode rejects named module units (for example files with
# `export module ...;`), because wrapper shims include original sources.
# Manifest mode also rejects named module units.
# For now, pass explicit SOURCES that exclude module units.
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

# Alternative: run everything in a single process.
# add_test(NAME my_tests COMMAND my_tests)
```

Docs: [Death tests](docs/death_tests.md), [CTest discovery options](docs/discover_tests.md).

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

## C++20 modules

Install package with module support (`find_package` consumer flow):

```bash
cmake -S . -B build -DGENTEST_ENABLE_MODULES=ON -Dgentest_INSTALL=ON
cmake --build build --target install
```

Consumer `CMakeLists.txt`:

```cmake
find_package(gentest CONFIG REQUIRED)

add_executable(my_tests cases.cpp)
target_link_libraries(my_tests PRIVATE
    gentest::gentest_main
    gentest::gentest_modules)
```

Consumer source:

```cpp
#include <span>
#include <string_view>
import gentest;
```

Notes:
- `gentest::gentest_modules` exports the `gentest` named module; link it alongside `gentest::gentest_main` (or `gentest::gentest_runtime` if you provide your own `main`).
- `gentest_attach_codegen()` currently cannot scan/wrap named module units (interfaces or implementations) in either TU wrapper or manifest mode. Pass explicit `SOURCES` that exclude module units.
- Mock generation is include-based today (`#include "gentest/mock.h"` plus generated registry/impl headers). `import gentest;` does not export mock APIs, and complete module generation for mocks is still incomplete.
- Some toolchains require module scanning to be enabled in the consumer project (`CMAKE_CXX_SCAN_FOR_MODULES=ON` or target property `CXX_SCAN_FOR_MODULES ON`).
- `import std;` support is compiler/STL dependent. Use normal standard-library includes in consumer TUs unless your toolchain supports `import std;`. Optional configure probe: `-DGENTEST_TRY_IMPORT_STD=ON`.

`--list-tests` prints only resolved test names (one per line).
`--list` prints the richer listing format (name plus metadata such as tags/owner when present).
`--kind` restricts execution/filtering to `all|test|bench|jitter` (default `all`).

Naming:
- Any gentest function-level attribute marks the declaration as a case.
- `test("...")` is optional; if omitted, the base name defaults to the C++ function name (or `FixtureType/method` for member tests).
- Use `test("...")` to disambiguate overloads and keep names stable across refactors.

Tags/metadata:
- Flag attributes are collected as tags: `fast`, `slow`, `linux`, `windows`, `death`.
- Value attributes attach metadata: `req("BUG-123")`, `owner("team-runtime")`, `skip("reason")`.
- `req("...")` is the requirement-to-test mapping hook for traceability workflows (see [docs/traceability_standards.md](docs/traceability_standards.md)).
- Requirement IDs are shown in `--list` output as `requires=...` and exported in JUnit as
  `<property name="requirement" value="...">`, so you can build a trace matrix from CI artifacts (example flow: [docs/traceability_standards.md](docs/traceability_standards.md)).
- Source standards links: [ISO 26262-6](https://www.iso.org/standard/68388.html), [ISO 26262-8](https://www.iso.org/standard/68390.html), [IEC 61508-1](https://webstore.iec.ch/en/publication/5515), [IEC 61508-3](https://webstore.iec.ch/en/publication/5517), [IEC TS 61508-3-1](https://webstore.iec.ch/en/publication/25410).
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

[[using gentest: test]]
void add() {
    EXPECT_EQ(1 + 1, 2);
    ASSERT_TRUE(2 + 2 == 4, "fatal: aborts the current test");
}

} // namespace math
```

Exceptions:
- If a test throws a `std::exception`, the runner records a failure like `unexpected std::exception: ...` and continues.
- If you want to assert on exceptions, you can use the gtest-like macros.

```cpp
#include "gentest/attributes.h"
#include "gentest/runner.h"
using namespace gentest::asserts;

#include <stdexcept>

[[using gentest: test("exceptions/macros")]]
void macros() {
    EXPECT_THROW(throw std::runtime_error("boom"), std::runtime_error);
    EXPECT_THROW(throw 123, int);
    EXPECT_NO_THROW((void)0);
}
```

- For more detailed checks (e.g. matching `what()`), use normal C++ `try`/`catch` and `gentest::fail(...)`:

```cpp
#include "gentest/attributes.h"
#include "gentest/runner.h"

#include <exception>
#include <string_view>

[[using gentest: test("exceptions/handled")]]
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

[[using gentest: test("outcomes/skip")]]
void skip_example() {
    gentest::skip("not supported on this configuration");
}

[[using gentest: test("outcomes/xfail")]]
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

[[using gentest: test("concurrency/adopt_and_log")]]
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

### Parameters (value matrices)

Use named parameter axes to generate a Cartesian product of value sets:

```cpp
#include "gentest/attributes.h"
#include "gentest/runner.h"

[[using gentest: test("params/pairs")]]
[[using gentest: parameters(a, 1, 2)]]
[[using gentest: parameters(b, 10, 20)]]
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

Supported fixture argument forms:
- `T&`
- `T*`
- `std::shared_ptr<T>`

Allocation hooks can return `std::unique_ptr<T>` (or custom-deleter unique ptr),
`std::shared_ptr<T>`, or raw `T*`. Gentest always manages fixture ownership
internally. Raw-pointer returns are adopted into managed ownership (same lifetime
semantics as returning `std::unique_ptr<T>`), and the framework handles
deallocation.

```cpp
#include "gentest/attributes.h"
#include "gentest/fixture.h"
#include "gentest/runner.h"
using namespace gentest::asserts;

struct Counter : gentest::FixtureSetup {
    int x = 0;
    void setUp() override { x = 1; }
};

[[using gentest: test("fx/counter")]]
void counter(Counter& c) {
    EXPECT_EQ(c.x, 1);
}
```

See [docs/fixtures_allocation.md](docs/fixtures_allocation.md) for the full allocation and ownership model.

### Mocks

`gentest::mock<T>` is generated from your test sources. It works for both virtual interfaces *and* non-virtual types
(you don’t need `virtual` APIs just to mock them).

Virtual interface example:

```cpp
#include "gentest/attributes.h"
#include "clock.h" // header that defines Clock
#include "gentest/mock.h"
using namespace gentest::asserts;

int read_now(Clock& c) { return c.now(); }

[[using gentest: test("mock/clock")]]
void mock_clock() {
    gentest::mock<Clock> clock;
    EXPECT_CALL(clock, now).times(1).returns(123);
    EXPECT_EQ(read_now(clock), 123);
}
```

Safeguards:
- Mocked target definitions must be in an includable header (diagnostics: "header or header module", including header units). Definitions in ordinary source files and named module units are rejected by codegen (the generated mock registry currently resolves targets via `#include`, not `import`).
- Header-like files with nonstandard extensions (for example `.mpp`) are accepted when treated as headers (not as named module units).
- `gentest_codegen` emits required definition-header includes into the generated mock registry, so `gentest/mock.h` can resolve mocks without strict include order.
- Generated mock-registry includes are relative when possible and fall back to absolute paths for cross-root/cross-drive headers (Windows-only path constraint).

Header implementation to mock (`sink.h`):

```cpp
#pragma once

struct Sink {
    void write(int value) { last = value; }
    int last = 0;
};
```

Test file (`cases.cpp`):

```cpp
#include "gentest/attributes.h"
#include "sink.h" // header that defines Sink::write(int)
#include "gentest/mock.h"
using namespace gentest::asserts;

template <class SinkLike>
void emit(SinkLike& s) {
    s.write(7);
}

[[using gentest: test("mock/nonvirtual")]]
void mock_nonvirtual() {
    gentest::mock<Sink> sink;
    EXPECT_CALL(sink, write).times(1).with(7);
    emit(sink);
}
```

Matchers (`.where(...)` with `gentest::match` helpers):

```cpp
#include "sink.h"
#include "gentest/mock.h"
using namespace gentest::match;

gentest::mock<Sink> sink;
EXPECT_CALL(sink, write).times(2).where(InRange(10, 20));
```

Static member functions can be mocked too when the call goes through the mock object:

```cpp
struct Ticker {
    static int add(int lhs, int rhs) noexcept { return lhs + rhs; }
};

gentest::mock<Ticker> t;
EXPECT_CALL(t, add).times(1).returns(123);
EXPECT_EQ(t.add(4, 5), 123);
EXPECT_EQ(Ticker::add(4, 5), 9); // direct static call (not mocked)
```

`t.add(...)` is intercepted. `Ticker::add(...)` bypasses the mock.

### Benchmarks and jitter

Define microbenchmarks and jitter benchmarks (for timing variance):

```cpp
#include "gentest/attributes.h"
#include "gentest/bench_util.h"

#include <cmath>
#include <string>

[[using gentest: bench("bench/concat")]]
void bench_concat() {
    std::string s = "hello";
    s += " world";
    gentest::doNotOptimizeAway(s);
}

[[using gentest: jitter("bench/sin")]]
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

>[!WARNING]
> Cross-compiling requires a *host* `gentest_codegen` executable. See
> [`docs/cross_compile/linux_aarch64_qemu.md`](docs/cross_compile/linux_aarch64_qemu.md) and the install templates under
> [`docs/install/`](docs/install/).

## Docs

- Docs index: [`docs/index.md`](docs/index.md)
- Install templates: [`Linux`](docs/install/linux.md), [`macOS`](docs/install/macos.md), [`Windows`](docs/install/windows.md)
