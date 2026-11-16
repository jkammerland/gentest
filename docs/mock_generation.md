# Mocks

This page shows how to generate mock classes for the types your code depends on
and set expectations on them. You name the types to mock in a mock definitions
header, turn it into a mock target with `gentest_add_mocks()`, and link that
target into your tests.

With gentest mocks (`BACKEND gentest`, the default), gentest's runner reports a
failed expectation like a failed assertion. With the gMock or Trompeloeil
backend, gentest only generates the mock classes, and your tests use that
framework directly.

## Quick start

This example mocks a clock and tests a function that reads it.
[examples/mocking](../examples/mocking) is a complete project.

`clock.hpp` is the production header:

```cpp
#pragma once

namespace app {

struct Clock {
    virtual ~Clock()        = default;
    virtual int now() const = 0;
};

inline int seconds_until(const Clock &clock, int deadline) { return deadline - clock.now(); }

} // namespace app
```

`clock_mocks.hpp` is the mock definitions header. It includes the production
header and `gentest/mock_fwd.h`, and names each type to mock with a
`gentest::mock<T>` alias:

```cpp
#pragma once

#include "clock.hpp"
#include "gentest/mock_fwd.h"

namespace app::mocks {
using ClockMock = gentest::mock<app::Clock>;
} // namespace app::mocks
```

`CMakeLists.txt` passes that header as `DEFS` to `gentest_add_mocks()`, which
creates the mock target `clock_mocks`, and links the mock target into the test
target:

```cmake
find_package(gentest CONFIG REQUIRED)

gentest_add_mocks(clock_mocks
    DEFS ${CMAKE_CURRENT_SOURCE_DIR}/clock_mocks.hpp
    HEADER_NAME public/clock_mocks.hpp)

add_executable(clock_tests cases.hpp)
target_link_libraries(clock_tests PRIVATE gentest::gentest_main clock_mocks)
gentest_attach_codegen(clock_tests)
gentest_discover_tests(clock_tests)
```

`cases.hpp` includes the generated header that `HEADER_NAME` names and sets an
expectation:

```cpp
#pragma once

#include "clock.hpp"
#include "gentest/test.h"
#include "public/clock_mocks.hpp"

using namespace gentest::asserts;

[[using gentest: test("clock/seconds_until")]]
inline void seconds_until_deadline() {
    app::mocks::ClockMock clock;
    EXPECT_CALL(clock, now).returns(100);

    EXPECT_EQ(app::seconds_until(clock, 130), 30);
}
```

`EXPECT_CALL(clock, now).returns(100)` expects exactly one call to `now()` and
makes it return 100. When `clock` goes out of scope, a wrong number of calls
fails the test.

Two rules apply to every mock target:

- Link the mock target with `target_link_libraries()` before you call
  `gentest_attach_codegen()` on the test target, as above, or with
  [`gentest_link_mocks()`](#link-mock-targets) before or after that call.
  Either way, the test's codegen runs after the generated mock header exists.
- `gentest::mock<T>` is a generated class only for the types that a linked mock
  target names in its `DEFS`. Your alias is a short name for that class:
  `gentest::mock<app::Clock>` and `app::mocks::ClockMock` are the same type.

## Expectations

Set an expectation with `EXPECT_CALL(mock, method)` or
`gentest::expect(mock, &Type::method)`, then chain the options you need. Both
return an expectation handle that you can keep and configure further, until the
mock's first call.

The examples in this section use `app::mocks::StoreMock store;`, a mock of this
interface declared like `ClockMock`, which the code under test calls through
`app::Store &store_ref = store;`:

```cpp
#pragma once

#include <string>
#include <string_view>

namespace app {

struct Store {
    virtual ~Store()                                  = default;
    virtual bool put(std::string_view key, int value) = 0;
    virtual int  get(std::string_view key) const      = 0;
    virtual int &slot(int index)                      = 0;
    virtual bool load(std::string &out)               = 0;
};

} // namespace app
```

### Call counts

| Option | Calls expected |
|---|---|
| no option | exactly one |
| `.times(n)` | exactly `n`; `.times(0)` forbids calls |
| `.allow_more()` | the configured number or more |

A call beyond the expected number fails at once with
`unexpected call to <method>`. Missing calls are reported when the mock is
destroyed.

Several expectations on one method are used in the order you declare them. Each
one takes its `.times(n)` calls, then the next one takes over:

```cpp
EXPECT_CALL(store, get).returns(1);          // the first call returns 1
EXPECT_CALL(store, get).times(2).returns(2); // the next two calls return 2
```

Arguments don't choose between expectations: each call is checked against the
one whose turn it is. An expectation with `.allow_more()` keeps taking calls, so
later ones on the same method are never used.

### Arguments

| Option | Checks |
|---|---|
| `.with(values...)` | each argument with `==` |
| `.where(matchers...)` | each argument with a matcher or a predicate |
| `.where_call(predicate)` | all arguments together |

```cpp
EXPECT_CALL(store, put).with("answer", 42).returns(true);

using namespace gentest::match;
EXPECT_CALL(store, put).where(StartsWith("user:"), InRange(0, 100)).returns(true);

EXPECT_CALL(store, put)
    .where_call([](std::string_view key, int value) { return key.size() <= 8 && value >= 0; })
    .returns(true);
```

- `.with(...)` takes one value per parameter and keeps its own copy, so a
  temporary such as `.with(fmt::format(...))` is safe for a `std::string_view`
  parameter. String parameters (`std::string`, `std::string_view`,
  `const char *`, `char *`) compare by content; a null `const char *` equals
  only `nullptr`. Other values are converted to the parameter type if needed. If
  the parameter type has no usable `==`, `.with(...)` doesn't compile.
- `.where(...)` takes one matcher per parameter: a `gentest::match` matcher such
  as `Any()`, `Eq(x)`, `InRange(lo, hi)` or `StartsWith(s)`, or a callable that
  takes the argument and returns `bool`. Assertions use the same matchers;
  [Failure diagnostics](failure_diagnostics.md) lists them all and shows how to
  write your own. `.where_args(...)` is another name for `.where(...)`.
- `.where_call(predicate)` passes all arguments, as const references, to one
  predicate.
- Only one kind of check applies: `.where_call(...)` if set, otherwise
  `.where(...)`, otherwise `.with(...)`.
- A call with wrong arguments fails the test, but it counts toward `.times(n)`
  and its action runs.

Parameter types need nothing unless you check them: they may lack
`operator==`, be non-copyable, or be references to abstract classes or streams.

### Results and actions

| Option | Effect |
|---|---|
| `.returns(value)` | returns a copy of `value` on each call |
| `.returns_ref(object)` | returns a reference to `object`, for methods that return an lvalue reference |
| `.invokes(callable)` | calls `callable` with the arguments and returns its result |

```cpp
int cell = 0;
EXPECT_CALL(store, slot).returns_ref(cell); // store_ref.slot(3) = 7 writes to cell

EXPECT_CALL(store, load).invokes([](std::string &out) {
    out = "cached";
    return true;
});
```

- `.returns(...)` doesn't compile for a method that returns a reference. A
  move-only value such as `std::make_unique<T>()` is moved out by the first
  call, and later calls get the moved-from value; use `.invokes(...)` to make a
  fresh object for each call.
- `.invokes(...)` passes the arguments as the method declares them, so the
  callable can assign through a non-const reference, as above, or take
  ownership of a by-value or rvalue-reference argument such as a
  `std::unique_ptr<Job>`. It can also take only const references. The callable
  is stored once, so a `mutable` lambda keeps its state between calls, and it
  may be move-only. Argument checks run before the action.

A call without an action returns a value-initialized result, such as `0`,
`false` or an empty string. If the method returns a reference or a type that
can't be value-initialized, the mock prints the reason to stderr and calls
`std::terminate()`, for example after `EXPECT_CALL(store, slot);`:

```text
gentest: no action is configured for ::app::Store::slot, and no result can be produced: the method returns a reference; configure returns_ref() or invokes() (expectation at <file>:<line>); terminating
```

An unexpected call in strict mode and an ignored call in nice mode end the same
way; their messages start `gentest: unexpected call to` and
`gentest: no expectation is configured for`. Give such a method an action for
every call the test can make.

### Strict and nice mocks

A gentest mock is strict: a call that no expectation accepts fails the test with
`unexpected call to <method>`. `gentest::make_nice(mock)` relaxes this for the
methods of that mock that have no expectations: their calls are ignored and
return a value-initialized result. `gentest::make_strict(mock)` switches back.

```cpp
app::mocks::StoreMock store;
gentest::make_nice(store);
EXPECT_CALL(store, put).returns(true);
// get() has no expectation: its calls return 0 and don't fail the test.
// put() is checked as usual.
```

Nice mode doesn't change a method that has expectations. Calls beyond its
expected number, including any call to a `.times(0)` method, fail as in strict
mode; add `.allow_more()` to accept them.

### Select the method

- `EXPECT_CALL(mock, name)` takes the bare method name, and the name must not be
  overloaded. Select one overload by casting the member pointer, for example
  `write(int)` of an overloaded `app::Logger::write`:
  `gentest::expect(logger, static_cast<void (app::Logger::*)(int)>(&app::Logger::write))`.
- Name a member function template with its template arguments:
  `EXPECT_CALL(codec, encode<int>)`. If the template arguments contain a comma,
  which a macro argument can't, use
  `gentest::expect<&T::name<A, B>>(mock, "name")`.
- With MSVC, `gentest::expect(mock, &T::method)` can't tell apart two mocked
  methods of `T` that have the same signature, and it fails with
  `ambiguous mock method pointer`. `EXPECT_CALL` and
  `gentest::expect<&T::method>(mock, "name")` work with every compiler.
- `ASSERT_CALL` is another name for `EXPECT_CALL`. Neither one stops the test.

### Failure messages

Missing calls, argument mismatches, extra calls and rejected configuration
changes end with the location where you declared the expectation:

```text
expected 2 call(s) to get but observed 1 (expectation at <file>:<line>)
argument[1] mismatch for ::app::Store::put: expected 42, got 41 (expectation at <file>:<line>)
argument[0] mismatch for ::app::Store::put: expected prefix 'user:', got 'admin' (expectation at <file>:<line>)
unexpected call to ::app::Store::get (expectation at <file>:<line>)
```

Call-count and configuration messages name the method as you selected it: `get`
after `EXPECT_CALL(store, get)`, `::app::Store::get` after
`gentest::expect(store, &app::Store::get)`. Argument and unexpected-call messages
always use the qualified name. A call to a method that has no expectations has
no location, so it reads `unexpected call to ::app::Store::put`. Mock failures
also carry the scoped context described in
[Failure diagnostics](failure_diagnostics.md).

### Mocks and threads

Configure a mock on the test's own thread before the code under test starts
other threads. The configuration is frozen at the mock's first call; a later
`EXPECT_CALL`, `make_nice` or change through a saved handle fails the test and
leaves the mock as it was:

```text
Cannot configure mock put after its first invocation or verification. Configure expectations before using the mock. (expectation at <file>:<line>)
```

After that, any thread can call the mock. Actions aren't synchronized, so a
stateful action called from several threads must protect its own state.

> [!WARNING]
> Only the test's own thread can record a failure. A failing mock call on
> another thread aborts the whole run with
> `gentest: fatal: assertion/expectation recorded from an adopted test context.`
> on a thread that [adopted](logging_and_threads.md) the test's context, or
> `gentest: fatal: assertion/expectation recorded without an active test context.`
> on any other thread. Configuring a mock on an adopted thread aborts the run
> too. [Logging and threads](logging_and_threads.md) lists what a worker thread
> can do.

## Non-virtual, static and template members

gentest mocks cover every public and protected member function of the mocked
type, not only virtual ones:

- If the type has virtual functions, the mock derives from it and overrides
  them, so code that takes a `T &` or `T *` calls the mock. Non-virtual and
  static members reach the mock only when called through the mock type.
- If the type has no virtual functions, the mock doesn't derive from it, so the
  code under test must accept the mock type, for example as a template
  parameter.
- Call a mocked static member through the mock object, as in `sink.scale(7)`.
  `Sink::scale(7)` calls the real function.

`sink.hpp` has a type without virtual functions and a template that uses it:

```cpp
#pragma once

namespace app {

struct Sink { // no virtual functions
    void       write(int value) { last = value; }
    static int scale(int value) { return value * 2; }
    int        last = 0;
};

template <class SinkLike> void emit(SinkLike &sink, int value) { sink.write(sink.scale(value)); }

} // namespace app
```

With `using SinkMock = gentest::mock<app::Sink>;` in namespace `app::mocks` and
`HEADER_NAME public/sink_mocks.hpp`, a test passes the mock to the template:

```cpp
#pragma once

#include "gentest/test.h"
#include "public/sink_mocks.hpp"
#include "sink.hpp"

[[using gentest: test("sink/emit")]]
inline void emit_scales_then_writes() {
    app::mocks::SinkMock sink;
    EXPECT_CALL(sink, scale).with(7).returns(70);
    EXPECT_CALL(sink, write).with(70);

    app::emit(sink, 7);
}
```

More rules for members:

- A member function template is mocked per instantiation. Set the expectation
  on the instantiation that the code calls, such as
  `EXPECT_CALL(codec, encode<int>)`.
- The mock has the constructors of the mocked type, with the same `explicit` and
  `noexcept`. If the type has no default constructor, neither does the mock:
  `app::mocks::SeededMock seeded{42};`.
- A `final` virtual function isn't mocked; calls reach the real function.

The gMock and Trompeloeil backends reject static members and member function
templates; see [Limitations](#limitations).

## gMock and Trompeloeil

With `BACKEND gmock` or `BACKEND trompeloeil`, `gentest_add_mocks()` generates a
gMock or Trompeloeil mock class from the same mock definitions header, and your
tests use that framework directly. These backends work only through CMake
(with Ninja or Ninja Multi-Config) and only with header `DEFS`.

### gMock

```cmake
find_package(GTest CONFIG REQUIRED)

gentest_add_mocks(clock_gmock_mocks
    DEFS ${CMAKE_CURRENT_SOURCE_DIR}/clock_mocks.hpp
    HEADER_NAME public/clock_gmock_mocks.hpp
    BACKEND gmock
    LINK_LIBRARIES GTest::gmock)

add_executable(clock_gtest_tests clock_gtest_tests.cpp)
target_link_libraries(clock_gtest_tests PRIVATE clock_gmock_mocks GTest::gtest_main)
```

`clock_gtest_tests.cpp` is an ordinary GoogleTest source file:

```cpp
#include "clock.hpp"
#include "public/clock_gmock_mocks.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

TEST(Clock, SecondsUntil) {
    app::mocks::ClockMock clock;
    EXPECT_CALL(clock, now()).WillOnce(::testing::Return(100));

    EXPECT_EQ(app::seconds_until(clock, 130), 30);
}
```

### Trompeloeil

```cmake
find_package(trompeloeil CONFIG REQUIRED)

gentest_add_mocks(clock_trompeloeil_mocks
    DEFS ${CMAKE_CURRENT_SOURCE_DIR}/clock_mocks.hpp
    HEADER_NAME public/clock_trompeloeil_mocks.hpp
    BACKEND trompeloeil
    LINK_LIBRARIES trompeloeil::trompeloeil)

add_executable(clock_trompeloeil_tests clock_trompeloeil_tests.cpp)
target_link_libraries(clock_trompeloeil_tests PRIVATE clock_trompeloeil_mocks trompeloeil::trompeloeil)
```

`clock_trompeloeil_tests.cpp` is a standalone program:

```cpp
#include "clock.hpp"
#include "public/clock_trompeloeil_mocks.hpp"

#include <trompeloeil/mock.hpp>

int main() {
    app::mocks::ClockMock clock;
    REQUIRE_CALL(clock, now()).RETURN(100);
    return app::seconds_until(clock, 130) == 30 ? 0 : 1;
}
```

### Generated class names

The generated class is named after the mocked type, not after your alias:
`app::Clock` becomes `app::mocks::ClockMock`, and `Clock` at global scope becomes
`mocks::ClockMock`. Tests never see your alias. Name the alias
`<namespace>::mocks::<Type>Mock`, as in the quick start, and tests use the same
name with every backend.

Other differences from gentest mocks:

- The gentest mock API (`.returns(...)`, `.with(...)`, `make_nice` and the
  rest) doesn't apply.
- For a type with virtual functions the generated class is `final`, so gMock's
  `testing::NiceMock` and `testing::StrictMock` can't wrap it. gMock warns about
  calls to methods that have no expectations; run the tests with
  `--gmock_default_mock_behavior=2` to make such calls fail, or with
  `--gmock_default_mock_behavior=0` to allow them without a warning.
- Calls that rely on default arguments work, because the generated class adds
  a forwarding overload for each default.
- The Trompeloeil backend emits explicit-arity `MAKE_MOCKn` and
  `MAKE_CONST_MOCKn` macros, which older Trompeloeil packages accept.

### Execution and reports

With these backends, test registration, failures, exit status and reports
belong to the framework that runs the tests. Register the gMock example's cases
with CTest through CMake's GoogleTest module:

```cmake
include(CTest)
enable_testing()
include(GoogleTest)
gtest_discover_tests(clock_gtest_tests)
```

```bash
ctest --test-dir build --output-on-failure
./build/clock_gtest_tests --gtest_filter=Clock.SecondsUntil --gtest_output=xml:gtest.xml
```

Trompeloeil has no test runner. The standalone `main()` above uses
Trompeloeil's default reporting of violated expectations; register it as one
CTest test with `add_test(NAME clock_trompeloeil_tests COMMAND clock_trompeloeil_tests)`.

These mocks don't report into gentest's case results or its JUnit and Allure
reports. A custom runner must initialize the framework, keep its failure status
and return a nonzero exit code when expectations fail; the
[doctest and gMock consumer](../tests/cmake/third_party_mock_consumers/doctest_consumer.cpp)
calls `InitGoogleMock` and checks `testing::Test::HasFailure()` after doctest
runs. Use gentest mocks when you want gentest's runner and reports to record
expectations.

## `gentest_add_mocks()` reference

```cmake
gentest_add_mocks(<target>
    DEFS <file>...
    [HEADER_NAME <path>]
    [OUTPUT_DIR <dir>]
    [BACKEND gentest|gmock|trompeloeil]
    [LINK_LIBRARIES <item>...]
    [DEPENDS <target-or-file>...]
    [BASE_DIRS <dir>...]
    [CLANG_ARGS <arg>...]
    [MODULE_NAME <name>])
```

`gentest_add_mocks()` creates `<target>` as a static library that holds the
generated mocks.

| Argument | Meaning |
|---|---|
| `DEFS` | Required. The mock definitions headers or, for [named-module mocks](#named-module-mocks), the module interface units (`.cppm`, `.ccm`, `.cxxm`, `.ixx`, `.mxx`). One mock target takes headers or modules, not both. A file is a module only when everything after the first dot of its name is one of those extensions: `my.service.cppm` counts as a header. |
| `HEADER_NAME` | The path of the generated header, relative to `OUTPUT_DIR`; tests include it by this path. Without it, the name is the target name plus a hash, such as `clock_mocks_8ad56ebb45c0.hpp`, so set it to get a stable include path. Header `DEFS` only. |
| `OUTPUT_DIR` | Where the generated files go; a relative path is relative to `CMAKE_CURRENT_BINARY_DIR`. Default: `${CMAKE_CURRENT_BINARY_DIR}/gentest_mocks/<target>_<hash>`. Each mock target needs its own directory. With a multi-config generator, the files go to a subdirectory per configuration. |
| `BACKEND` | `gentest` (the default), `gmock` or `trompeloeil`. |
| `LINK_LIBRARIES` | Libraries linked `PUBLIC`: the ones whose headers the `DEFS` include, and the gMock or Trompeloeil target. Codegen runs after each entry that is already a target when `gentest_add_mocks()` runs. |
| `DEPENDS` | More targets or files that codegen must wait for; see [Generated inputs and `DEPENDS`](#generated-inputs-and-depends). |
| `BASE_DIRS` | The directories that `DEFS` paths are relative to; see [DEFS include paths and `BASE_DIRS`](#defs-include-paths-and-base_dirs). |
| `CLANG_ARGS` | Extra arguments for the Clang parse that `gentest_codegen` runs over the `DEFS`, such as `-D` flags that only codegen needs. They don't change how the mock library compiles. |
| `MODULE_NAME` | The name of the generated module that tests import. Required with module `DEFS`, rejected with header `DEFS`. |

`DEFS`, `OUTPUT_DIR` and `BASE_DIRS` don't accept generator expressions. A
gentest mock target links `gentest::gentest` publicly; a gMock or Trompeloeil
mock target doesn't.

### Link mock targets

`gentest_attach_codegen()` looks at the mock targets linked when you call it.
It makes the test target's codegen run after theirs, and it fails at configure
time if two of them would include different `DEFS` files by the same path.

`gentest_link_mocks()` links mock targets and works before or after
`gentest_attach_codegen()`:

```cmake
gentest_link_mocks(<target> [PRIVATE|PUBLIC|INTERFACE] <mock-target>...)
```

It links the mock targets into `<target>` like `target_link_libraries()`,
`PRIVATE` unless you name another scope, and runs the same `DEFS` check. If
`gentest_attach_codegen()` already ran for `<target>`, it also gives that
codegen what it takes from mock targets linked before it: the codegen runs
after theirs and reads their generated files and named-module sources.
[examples/mocking](../examples/mocking/CMakeLists.txt) uses it.

A `target_link_libraries()` call after `gentest_attach_codegen()` links the
mock target, but the test's codegen doesn't wait for the mock target's codegen,
and the `DEFS` check doesn't run. Use `gentest_link_mocks()` there instead.

A test target can link several mock targets, and several test targets can link
one mock target.

### Other build systems

Meson, Xmake and Bazel have their own mock helpers, which generate gentest mocks
only; see the Mocks sections of [Meson](buildsystems/meson.md),
[Xmake](buildsystems/xmake.md) and [Bazel](buildsystems/bazel.md).

### DEFS are public headers

`gentest_add_mocks()` uses `DEFS` files in place, without copying them. With
gentest mocks, the generated header includes your `DEFS` file, which includes
the production headers, so a test can include the production header and the
mock header in either order.

Whatever a `DEFS` file includes must be reachable by the targets that link the
mock target. Production headers normally come from a library passed in
`LINK_LIBRARIES`. A header beside the `DEFS` can join the `DEFS` file set, which
installs it with them:

```cmake
target_sources(clock_mocks PUBLIC
    FILE_SET gentest_explicit_mock_defs FILES ${CMAKE_CURRENT_SOURCE_DIR}/clock.hpp)
```

### DEFS include paths and `BASE_DIRS`

Each `DEFS` file has an include path: its path relative to a base directory.
Each base directory becomes a public include directory of the mock target and a
base directory of the `gentest_explicit_mock_defs` file set. With gentest mocks,
the generated header includes each `DEFS` file by its include path, with angle
brackets.

By default, a `DEFS` file under `CMAKE_CURRENT_BINARY_DIR` is relative to that
directory, and any other `DEFS` file is relative to `CMAKE_CURRENT_SOURCE_DIR`,
so `DEFS a/mocks.hpp` is included as `<a/mocks.hpp>`. A `DEFS` file outside both
directories is a configure error until you name its base directory.

`BASE_DIRS <dir>...` names the base directories yourself, for `DEFS` outside
those directories or to expose a narrower include directory:

```cmake
gentest_add_mocks(clock_mocks
    DEFS ${PROJECT_SOURCE_DIR}/include/app/clock_mocks.hpp # included as <app/clock_mocks.hpp>
    BASE_DIRS ${PROJECT_SOURCE_DIR}/include
    HEADER_NAME public/clock_mocks.hpp)
```

Relative `BASE_DIRS` entries are relative to `CMAKE_CURRENT_SOURCE_DIR`. Every
`DEFS` file must be inside one entry, entries must not contain each other (nor
may the defaults, when the build directory is inside the source directory and
`DEFS` come from both), and a filesystem root is rejected. Two `DEFS` files of
one mock target can't have the same include path.

Two gentest mock targets used by one test target must not include different
`DEFS` files by the same path, because the compiler would find only the first.
`gentest_attach_codegen()` and `gentest_link_mocks()` report this at configure
time, and the generated headers refuse to compile together. Give one target a
different `BASE_DIRS`, or rename a file; the same holds for two mock targets
installed into one prefix. `HEADER_NAME` may share a `DEFS` file's name in
another directory, but not its full include path.

### Generated inputs and `DEPENDS`

Mock codegen parses the `DEFS` and everything they include. It already runs
after the `DEFS` files themselves and every `LINK_LIBRARIES` entry that is a
target when `gentest_add_mocks()` runs. For other generated headers, such as
one from a target defined later, pass the target or file in `DEPENDS`:

```cmake
gentest_add_mocks(clock_mocks
    DEFS clock_mocks.hpp
    DEPENDS generate_clock_headers)
```

## Named-module mocks

To mock types that named modules define, pass the module interface units as
`DEFS` and set `MODULE_NAME`; tests import the generated module
(`import my.service_mocks;`) instead of including a header. Module `DEFS` work
only with gentest mocks, can't be mixed with header `DEFS`, and don't take
`HEADER_NAME`. [Named modules](modules.md) has the complete example.

## Install and export a mock target

Install a mock target with its two file sets: `gentest_explicit_mock_headers`
(the generated headers) and `gentest_explicit_mock_defs` (your `DEFS`). Install
the headers that the `DEFS` include too, through their library or the `DEFS`
file set ([DEFS are public headers](#defs-are-public-headers)).

```cmake
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS clock_mocks
    EXPORT clock_mocksTargets
    FILE_SET gentest_explicit_mock_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILE_SET gentest_explicit_mock_defs DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT clock_mocksTargets
    NAMESPACE app::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/clock_mocks)

configure_package_config_file(clock_mocksConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/clock_mocksConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/clock_mocks)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/clock_mocksConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/clock_mocks)
```

The package config file finds what the mock target links. A gentest mock target
links `gentest::gentest`, so `clock_mocksConfig.cmake.in` is:

```cmake
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)
find_dependency(gentest CONFIG REQUIRED)

include("${CMAKE_CURRENT_LIST_DIR}/clock_mocksTargets.cmake")
```

A consumer project links the exported target and includes
`public/clock_mocks.hpp` as before:

```cmake
find_package(gentest CONFIG REQUIRED)
find_package(clock_mocks CONFIG REQUIRED)

add_executable(clock_tests cases.hpp)
target_link_libraries(clock_tests PRIVATE gentest::gentest_main app::clock_mocks)
gentest_attach_codegen(clock_tests)
```

A gMock or Trompeloeil mock target doesn't link `gentest::gentest`; its package
config file calls `find_dependency(GTest CONFIG REQUIRED)` or
`find_dependency(trompeloeil CONFIG REQUIRED)` instead.

A mock target with module `DEFS` has other file sets; see
[Named modules](modules.md).

## Limitations

These rules apply to every backend:

- The mocked type must be a class or struct, defined in a header or in a
  named-module interface unit. A header is any file without a source-file
  extension (`.c`, `.cc`, `.cp`, `.cpp`, `.cxx`, `.c++`, `.cu`, `.m`, `.mm`), so
  a header with another extension, such as `.mpp`, works. A type defined in a
  source file fails with `target definition must be in a header or header module`.
- The mocked type must be complete where the `gentest::mock<T>` alias names it.
- Not supported: unions, `final` classes, types in an anonymous namespace,
  local classes, types with a private destructor, and types that have virtual
  functions but no accessible constructor other than a copy or move
  constructor.
- Only public and protected member functions are mocked. Private and deleted
  ones are left out, and so are non-pure `final` virtual functions.
- Rejected member functions: private pure virtual functions, `final` pure
  virtual functions, conversion operators, C-style variadic functions,
  `volatile`-qualified functions and pure virtual assignment operators.

gentest mocks also have these limits:

- A method named `mock` is rejected, because the generated class is itself
  named `mock`. Rename the method, or use the gMock or Trompeloeil backend.
- A type that a named module defines must follow the rules in
  [Named modules](modules.md): namespace scope, defined in the interface unit
  itself.

The gMock and Trompeloeil backends also have these limits:

- Header `DEFS` only, through CMake only.
- The `gentest::mock<T>` alias must be in a different header from the definition
  of `T`. Otherwise codegen fails with
  `third-party mock targets must be declared in a header separate from the mocked target definition`.
- Rejected: nested types (a class declared inside a class), class template
  specializations, static member functions, member function templates,
  overloaded operators, and default arguments whose forwarding overload would
  collide with another overload of the method.
- Trompeloeil only: ref-qualified member functions (`&` or `&&`) are rejected.

## How generated mocks are organized

A mock target writes to `OUTPUT_DIR` the header named by `HEADER_NAME` and the
headers it includes: `<id>_mock_registry.hpp`, `<id>_mock_impl.hpp`, and one
`..._domain_<n>_<label>.hpp` pair per visibility domain (one for header-defined
types, one per named module), where `<id>` is the target name plus a hash. The
`gentest_explicit_mock_headers` file set installs them. They include each mocked
type's header relative to the mock target's include directories when they can,
so they work in the build tree and after installation. Otherwise the include is
relative to the generated file, or an absolute path when no relative path exists
(a header on another Windows drive).
