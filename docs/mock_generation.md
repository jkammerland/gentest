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

## Install / Export

Install the generated mock target like any CMake target:

```cmake
install(TARGETS clock_gmock_mocks
  EXPORT clock_gmock_mocksTargets
  FILE_SET gentest_explicit_mock_headers DESTINATION include)

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
return a value-initialized result (methods returning a reference terminate). Once a method has an expectation, nice mode does
not change its checking. Calls beyond the configured count, including any call to a `.times(0)` method, fail with `unexpected
call to <method>` and the expectation's declaration location, as in strict mode. Use `.allow_more()` to accept extra calls.
`gentest::make_strict(mock)` restores strict mode. Both apply to the default gentest backend; GMock and Trompeloeil mocks use
their own mechanisms, such as `testing::NiceMock`.

Before 2.0.0, nice mode also ignored calls after a method's expectations were used up, so tests that relied on that now fail.

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
