# gentest

`gentest` is an attribute-driven C++ test runner + code generator.

Write tests using standard C++ attributes (`[[using gentest: ...]]`). The build runs `gentest_codegen` to generate
`test_impl.cpp`, then your test binary runs `gentest::run_all_tests`.

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

[[using gentest: test("basic")]]
void basic() { EXPECT_TRUE(1 + 1 == 2); }

} // namespace demo
```

`main.cpp`:

```cpp
#include "gentest/runner.h"

int main(int argc, char** argv) { return gentest::run_all_tests(argc, argv); }
```

`CMakeLists.txt`:

```cmake
add_executable(my_tests main.cpp)
target_link_libraries(my_tests PRIVATE gentest::gentest)
gentest_attach_codegen(my_tests
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test_impl.cpp
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cases.cpp)
add_test(NAME my_tests COMMAND my_tests)
```

Run:

```bash
./my_tests --list-tests
./my_tests
```

## Feature examples

### Assertions, failures, and exceptions

`gentest` provides lightweight assertion helpers in `gentest::asserts` (inline functions, not macros):

```cpp
#include "gentest/attributes.h"
#include "gentest/runner.h"
using namespace gentest::asserts;

namespace math {

[[using gentest: test("add")]]
void add() {
    EXPECT_EQ(1 + 1, 2);
    ASSERT_TRUE(2 + 2 == 4, "fatal: aborts the current test");
}

} // namespace math
```

Exceptions:
- If a test throws a `std::exception`, the runner records a failure like `unexpected std::exception: ...` and continues.
- If you want to assert on exceptions, use normal C++ `try`/`catch` and `gentest::fail(...)`:

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

To test these “death” paths, run the case in its own process (e.g. via CTest):

```cmake
add_test(NAME my_death_case COMMAND my_tests --run-test=death/fatal_path)
set_tests_properties(my_death_case PROPERTIES WILL_FAIL TRUE)
```

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

Member-function tests run on an instance of the enclosing type:
- Ephemeral (default): fresh instance per member test.
- Suite: `[[using gentest: fixture(suite)]]` shares one instance for the suite.
- Global: `[[using gentest: fixture(global)]]` shares one instance across the full run.

```cpp
#include "gentest/attributes.h"
#include "gentest/fixture.h"
#include "gentest/runner.h"

struct [[using gentest: fixture(suite)]] Counter : gentest::FixtureSetup {
    int x = 0;
    void setUp() override { x = 1; }

    [[using gentest: test("fx/a")]] void a() { gentest::expect_eq(x, 1); }
    [[using gentest: test("fx/b")]] void b() { gentest::expect_eq(x, 1); }
};
```

Free-function fixture composition (always ephemeral):

```cpp
struct Db { /* ... */ };

[[using gentest: test("fx/free"), fixtures(Db)]]
void free_case(Db& db) { (void)db; }
```

### Mocks

`gentest::mock<T>` is generated from your test sources. It works for both virtual interfaces *and* non-virtual types.

Preconditions (enforced by codegen):
- `T` must be complete at the `gentest::mock<T>` site in a scanned source.
- `T` must be default-constructible, non-final, and not in an anonymous namespace / local class; destructor must not be private.
- Static member functions are not currently mockable.

Virtual interface example (define the interface in a header, then include it before `gentest/mock.h`):

```cpp
#include "gentest/attributes.h"
#include "clock.h" // virtual interface under test
#include "gentest/mock.h"
using namespace gentest::asserts;

[[using gentest: test("mock/clock")]]
void mock_clock() {
    gentest::mock<Clock> clock;
    EXPECT_CALL(clock, now).times(1).returns(123);

    EXPECT_EQ(clock.now(), 123);
}
```

For virtual interfaces, `gentest::mock<T>` derives from `T`, so you can also pass it to code under test as `T&`/`T*`
when needed (e.g. DI: `Clock* iface = &clock;`).

Non-virtual type example (the mock does not derive from `T`; use it directly, typically via templates/CRTP):

```cpp
#include "gentest/attributes.h"
#include "gentest/mock.h"
using namespace gentest::asserts;

struct Sink {
    void write(int) {}
};

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
./my_tests --run-bench=bench/concat
./my_tests --run-jitter=bench/sin --jitter-bins=20
```

>[!WARNING]
> Cross-compiling requires a *host* `gentest_codegen` executable. See
> [`docs/cross_compile/linux_aarch64_qemu.md`](docs/cross_compile/linux_aarch64_qemu.md) and the install templates under
> [`docs/install/`](docs/install/).

## Docs

- Docs index: [`docs/index.md`](docs/index.md)
- Install templates: [`Linux`](docs/install/linux.md), [`macOS`](docs/install/macos.md), [`Windows`](docs/install/windows.md)
