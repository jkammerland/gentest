# Mock Generation

`gentest_add_mocks()` can generate mock classes as a standalone CMake codegen step. For GMock and Trompeloeil, gentest is only the generator; your tests use the third-party framework directly.

## GMock

Interface:

```cpp
// clock.hpp
#pragma once

namespace app {
struct Clock {
    virtual ~Clock() = default;
    virtual int now() const = 0;
};
} // namespace app
```

Mock marker:

```cpp
// clock_mocks.hpp
#pragma once

#include "clock.hpp"
#include "gentest/mock_fwd.h"

namespace app::test_markers {
using ClockMock = gentest::mock<Clock>;
}
```

CMake:

```cmake
find_package(gentest CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

gentest_add_mocks(clock_gmock_mocks
  DEFS ${CMAKE_CURRENT_SOURCE_DIR}/clock_mocks.hpp
  OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/mocks"
  HEADER_NAME public/clock_mocks.hpp
  BACKEND gmock
  LINK_LIBRARIES GTest::gmock)

add_executable(clock_tests clock_tests.cpp)
target_link_libraries(clock_tests PRIVATE clock_gmock_mocks GTest::gtest_main)
```

If `HEADER_NAME` is omitted, the generated public header is `<target>.hpp` under `OUTPUT_DIR`.

Consumer:

```cpp
#include "public/clock_mocks.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

TEST(ClockTests, ReadsNow) {
    app::mocks::ClockMock mock;
    app::Clock           *clock = &mock;

    EXPECT_CALL(mock, now()).WillOnce(::testing::Return(123));
    EXPECT_EQ(clock->now(), 123);
}
```

## Trompeloeil

```cmake
find_package(gentest CONFIG REQUIRED)
find_package(trompeloeil CONFIG REQUIRED)

gentest_add_mocks(clock_trompeloeil_mocks
  DEFS ${CMAKE_CURRENT_SOURCE_DIR}/clock_mocks.hpp
  OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/trompeloeil_mocks"
  HEADER_NAME public/clock_mocks.hpp
  BACKEND trompeloeil
  LINK_LIBRARIES trompeloeil::trompeloeil)

add_executable(clock_tests clock_tests.cpp)
target_link_libraries(clock_tests PRIVATE clock_trompeloeil_mocks trompeloeil::trompeloeil)
```

```cpp
#include "public/clock_mocks.hpp"

#include <trompeloeil/mock.hpp>

int main() {
    app::mocks::ClockMock mock;
    app::Clock           *clock = &mock;

    REQUIRE_CALL(mock, now()).RETURN(123);
    return clock->now() == 123 ? 0 : 1;
}
```

## Execution and reports

The backend selects the generated mock API. Test discovery, assertion failures,
exit status, and reports belong to the framework that runs the consumer.

The GMock example links `GTest::gtest_main`, uses `TEST`/`EXPECT_CALL`, and runs
with GoogleTest's CLI. Register its individual cases with CTest using CMake's
GoogleTest helper:

```cmake
include(CTest)
enable_testing()
include(GoogleTest)
gtest_discover_tests(clock_tests)
```

```bash
ctest --test-dir build --output-on-failure
./build/clock_tests --gtest_filter=ClockTests.ReadsNow --gtest_output=xml:gtest.xml
```

Trompeloeil provides mocking and a reporter interface, rather than a test runner.
The standalone `main()` above checks the returned value and uses Trompeloeil's
default expectation reporting. Register that program as a single CTest test with
`add_test(NAME clock_tests COMMAND clock_tests)`. For a host test framework, use
that framework's Trompeloeil reporter integration and test registration/report
commands.

Generated third-party mocks do not forward failures into Gentest's case results
or its JUnit/Allure exporters. Linking a generated mock target does not install a
failure adapter. A custom runner must initialize the selected framework, retain
its failure status, and return a nonzero process status when expectations fail;
for example, the repository's [doctest/GMock consumer](../tests/cmake/third_party_mock_consumers/doctest_consumer.cpp)
calls `InitGoogleMock` and checks `testing::Test::HasFailure()` after doctest runs.
Use the default Gentest backend when you want expectations recorded directly by
Gentest's runner and reports.

## Naming

The generated third-party mock name comes from the mocked target type, not the marker alias:

```cpp
namespace app::test_markers {
using AnyAlias = gentest::mock<app::Clock>;
}

// Generated:
// app::mocks::ClockMock
```

A global target becomes `mocks::TypeMock`.

## DEFS are public headers

`gentest_add_mocks()` uses `DEFS` files in place; it does not copy them or the headers they include. The generated
public header includes your DEFS file, and the DEFS file includes the production headers it mocks, so a test can
include the production header and the mock header together, in either order, with include guards or `#pragma once`.

Treat a DEFS file like any public header of the mock target: what it includes must be reachable by consumers.
Production headers normally come from a library passed in `LINK_LIBRARIES`.

### DEFS include paths and `BASE_DIRS`

The generated public header includes each DEFS file with angle brackets, by its path relative to a base directory.
Each base directory becomes a public include directory of the mock target and a base directory of the
`gentest_explicit_mock_defs` file set, which installs the DEFS.

By default a DEFS file under `CMAKE_CURRENT_BINARY_DIR` is spelled relative to that directory, and any other DEFS
file relative to `CMAKE_CURRENT_SOURCE_DIR`. `DEFS a/mocks.hpp` is included as `<a/mocks.hpp>`, so it stays distinct
from `b/mocks.hpp` of another mock target, and a DEFS file generated into the build tree never carries the build
directory's name. A DEFS file outside both directories is a configure error until you name its base.

`BASE_DIRS <dir>...` names the base directories yourself. Use it for DEFS outside the current directories, or to
expose a narrower directory than the whole current source directory:

```cmake
gentest_add_mocks(clock_mocks
  DEFS ${PROJECT_SOURCE_DIR}/include/app/clock_mocks.hpp   # included as <app/clock_mocks.hpp>
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include)
```

Relative `BASE_DIRS` entries are relative to `CMAKE_CURRENT_SOURCE_DIR`. Every DEFS file must be inside one entry,
entries must not contain each other, and a filesystem root is rejected. The same holds for the defaults: if the build
directory is inside the source directory and DEFS come from both, pass `BASE_DIRS` naming separate directories.

Two mock targets used by one test must not include different DEFS files by the same path, because the compiler would
find only the first one. `gentest_attach_codegen()` and `gentest_link_mocks()` report this at configure time, and the
generated public headers refuse to compile together otherwise. Give one target a different `BASE_DIRS`, or rename a
file. Installing two mock targets into one prefix has the same requirement. `HEADER_NAME` may share a DEFS file's name
in another directory but not its full include path.

A header beside the DEFS can join the DEFS file set, which installs it with them:

```cmake
target_sources(clock_gmock_mocks PUBLIC
  FILE_SET gentest_explicit_mock_defs FILES ${CMAKE_CURRENT_SOURCE_DIR}/clock.hpp)
```

### Generated inputs and `DEPENDS`

Mock codegen parses the DEFS and everything they include, so generated headers must exist before it runs.
`gentest_add_mocks()` orders codegen after every `LINK_LIBRARIES` entry that is a target when it is called, and after
the DEFS files themselves, so a DEFS file written by `add_custom_command()` in the same directory just works. For
anything else, such as a header generated by a target that is defined later or is not linked, pass the target or
file in `DEPENDS`:

```cmake
gentest_add_mocks(clock_mocks
  DEFS clock_mocks.hpp
  DEPENDS generate_clock_headers)
```

## Install / Export

Install the generated mock target like any CMake target. Textual DEFS targets also install their DEFS file set:

```cmake
install(TARGETS clock_gmock_mocks
  EXPORT clock_gmock_mocksTargets
  FILE_SET gentest_explicit_mock_headers DESTINATION include
  FILE_SET gentest_explicit_mock_defs DESTINATION include)

install(EXPORT clock_gmock_mocksTargets
  NAMESPACE app::
  DESTINATION lib/cmake/clock_gmock_mocks)
```

For third-party backends, the exported target does not require consumers to link `gentest::gentest`. Consumers still need the generated header and the selected mock framework.

```cmake
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)
find_dependency(GTest CONFIG REQUIRED)

include("${CMAKE_CURRENT_LIST_DIR}/clock_gmock_mocksTargets.cmake")
```

Use `find_dependency(trompeloeil CONFIG REQUIRED)` for Trompeloeil packages.

## Nice and strict mode

Native mocks are strict by default: a call that no expectation accepts records `unexpected call to <method>`.
`gentest::make_nice(mock)` relaxes this only for methods that have no expectations on that mock; such calls are ignored and
return a value-initialized result (see [Arguments, actions and results](#arguments-actions-and-results) for methods that
cannot produce one). Once a method has an expectation, nice mode does
not change its checking. Calls beyond the configured count, including any call to a `.times(0)` method, fail with `unexpected
call to <method>` and the expectation's declaration location, as in strict mode. Use `.allow_more()` to accept extra calls.
`gentest::make_strict(mock)` restores strict mode. Both apply to the default gentest backend; GMock and Trompeloeil mocks use
their own mechanisms, such as `testing::NiceMock`.

Before 2.0.0, nice mode also ignored calls after a method's expectations were used up, so tests that relied on that now fail.

## Arguments, actions and results

These rules apply to the default gentest backend.

Mocking a method places no requirements on its parameter types: they may lack `operator==`, be non-copyable, or be references
to abstract classes or streams. Only the argument checks you ask for are instantiated.

- `.with(values...)` takes one value per parameter and compares with `==`. The expectation stores its own copy of each value,
  so a temporary such as `.with(fmt::format(...))` is safe for a `std::string_view` parameter. `std::string`,
  `std::string_view` and `const char *` parameters compare by content; a null `const char *` only equals `nullptr`. A value
  that the parameter cannot be compared to directly is converted to the parameter type first. A parameter without a usable
  `==` is a compile error at the `.with(...)` call; use `.where(...)` or `.where_call(...)` for it.
- `.invokes(callable)` calls the callable with the arguments as the method declares them: it can assign through a non-const
  reference (`[](int &out) { out = 1; }`) and take ownership of by-value and rvalue-reference arguments
  (`[](std::unique_ptr<Job> job) { ... }`). A callable that only accepts const references keeps working. The callable is
  stored once and called in place, so a `mutable` lambda keeps its state from call to call; it may be move-only. Argument
  checks run before the action and see the arguments unmodified.
- `.returns(value)` returns a copy of the value on every call. A move-only value such as `std::make_unique<T>()` is moved out
  by the first call that returns it; later calls get the moved-from value. Use `.invokes(...)` to produce a fresh object per
  call.
- `.returns_ref(object)` is required for methods that return a reference.

A call that has no action returns a value-initialized result. When that is impossible, because the method returns a reference
or a type that cannot be value-initialized, the mock prints the reason to stderr and calls `std::terminate()`. This also
applies to an unexpected call in strict mode and to an ignored call in nice mode: give such methods an expectation with
`.returns(...)`, `.returns_ref(...)` or `.invokes(...)` for every call the test can make.

Actions are not synchronized by the mock. A stateful action that is called from several threads must protect
its own state.

Method names: `EXPECT_CALL(mock, name)` needs a name that is not overloaded; select an overload with
`gentest::expect(mock, static_cast<R (T::*)(Args...)>(&T::name))`. A method named `mock` cannot be mocked by the gentest
backend, because the generated class is itself named `mock`; codegen reports it.

## Limitations

Configure native mock expectations and nice/strict mode before the first invocation or verification. Configuration is frozen
thereafter; adding an expectation or changing a saved expectation handle records a test failure and leaves the configured
behavior intact. Configure before starting worker threads, then dispatch concurrently as needed.

Native expectations retain their declaration location through both `gentest::expect(...)` and `EXPECT_CALL`. Count, argument,
excess-call, and rejected-configuration failures identify it. See [failure diagnostics](failure_diagnostics.md) for the shared
assertion/mock matcher API and contextual failures.

Third-party backends currently support CMake textual/header mock definitions only. `gentest_add_mocks()` supports single-config and multi-config generators; multi-config artifacts are emitted under a configuration subdirectory of `OUTPUT_DIR`.

The Trompeloeil backend emits explicit-arity `MAKE_MOCKn` / `MAKE_CONST_MOCKn` macros for compatibility with older Trompeloeil packages.

Use the default gentest backend for named-module mocks. Split or redesign targets that require nested target types, template-specialized target types, final classes, static methods, member function templates, final methods, private pure virtual methods, overloaded operators, or overloaded default-argument calls that would need the same generated forwarding signature.

No backend supports conversion operators, C-style variadic methods, volatile-qualified methods, or pure virtual assignment operators.
