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

## Limitations

Third-party backends currently support CMake textual/header mock definitions only. `gentest_add_mocks()` requires a single-config generator such as Ninja.

Use the default gentest backend for named-module mocks. Split or redesign targets that require nested target types, template-specialized target types, final classes, static methods, member function templates, final methods, private pure virtual methods, overloaded operators, or overloaded default-argument calls that would need the same generated forwarding signature.

No backend supports conversion operators, C-style variadic methods, volatile-qualified methods, or pure virtual assignment operators.
