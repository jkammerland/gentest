# Story: Exported Library Mock Packages For Downstream Consumers

## Goal

Make it straightforward for an application or library repository to keep its
own generated gentest mock surfaces checked in, packaged, and exported as a
separate consumer-facing CMake target.

The immediate goal of this story is not implementation. It is to define the
consumer-facing package shape we want to prove later with real producer and
consumer tests for both:

- textual mock surfaces
- named-module mock surfaces

This is the next downstream step after
[`014_explicit_mock_target_codegen.md`](014_explicit_mock_target_codegen.md)
and the existing installed explicit-mock export proof under
[`tests/cmake/explicit_mock_target_install_export/`](../../tests/cmake/explicit_mock_target_install_export).

## Why This Matters

The current explicit-mock target model is already better than implicit mock
discovery, but it still leaves too much responsibility with the downstream test
author:

- they still need to know which defs file owns which mocked types
- they still need to choose the right generated mock surface manually
- they still need to reconstruct mock-package structure on their own if they
  want to export mocks from their repo

That is fine for repo-local gentest tests. It is not a strong downstream story
for a real app/lib that wants to ship:

- its normal public API
- a stable optional gentest mock package
- later, other mock backends such as gmock or trompeloeil

The point is not only convenience. It is also dependency honesty. A consumer
test target should be able to say:

```cmake
target_link_libraries(my_tests PRIVATE acme::clock_gentest_mocks)
```

and get the complete compile-time surface without having to guess which header
or module defs file created that target.

## User Stories

### 1. Textual library mock package

As a library maintainer shipping a header-based API, I want to generate and
install a textual gentest mock package as its own CMake target, so downstream
test consumers can include one stable mock header path and link one stable mock
target.

### 2. Module library mock package

As a library maintainer shipping a named-module API, I want to generate and
install a named-module gentest mock package as its own CMake target, so
downstream test consumers can import one stable mock module and link one stable
mock target.

### 3. Downstream consumer simplicity

As a downstream consumer, I do not want to reverse-engineer which mocked types
or defs files I need. I want a library-owned mock target that already exposes
the correct public mock surface.

### 4. Future backend expansion

As a library maintainer, I want the packaging shape to be backend-aware so that
later we can add other generated mock backends, such as gmock or trompeloeil,
without redesigning the installed layout again.

## Proposed Package Shape

Each exported library owns its real API target and one or more backend-specific
mock targets.

Example imported targets:

- `acme::clock`
- `acme::clock_gentest_mocks`
- `acme::clock_module`
- `acme::clock_module_gentest_mocks`

The installed include layout should make the backend and purpose obvious:

```text
include/
  acme/clock.hpp
  acme/mock/gentest/clock_mocks.hpp
  acme/module/clock.cppm
  acme/module/mock/gentest/clock_mocks.cppm
```

The exact subdirectory names can still be debated, but the important contract
is:

- the library owns the mock install path
- the mock backend is encoded in the path
- consumers do not include generated files from anonymous build-tree roots

## CMake Solution Sketch

### Textual producer

```cmake
add_library(acme_clock INTERFACE)
target_include_directories(acme_clock INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

gentest_add_mocks(acme_clock_gentest_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/test_support/clock_mock_defs.hpp
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gentest/clock_mocks"
    LINK_LIBRARIES acme_clock
    HEADER_NAME acme/mock/gentest/clock_mocks.hpp)

install(TARGETS acme_clock
    EXPORT acme_clockTargets
    INCLUDES DESTINATION include)

install(TARGETS acme_clock_gentest_mocks
    EXPORT acme_clockTargets
    ARCHIVE DESTINATION lib
    FILE_SET gentest_explicit_mock_headers DESTINATION include
    INCLUDES DESTINATION include)
```

### Textual consumer

```cmake
find_package(acme_clock CONFIG REQUIRED)

add_executable(clock_consumer_tests
    main.cpp
    clock_cases.cpp)

target_link_libraries(clock_consumer_tests PRIVATE
    gentest::gentest_main
    acme::clock_gentest_mocks)

gentest_attach_codegen(clock_consumer_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gentest_codegen")
```

```cpp
#include <acme/mock/gentest/clock_mocks.hpp>

[[using gentest: test("consumer/clock/mock")]]
void uses_exported_textual_mock() {
    acme::mocks::ClockMock mock;
    (void)mock;
}
```

### Module producer

```cmake
add_library(acme_clock_module)
target_sources(acme_clock_module
    PUBLIC
        FILE_SET api TYPE CXX_MODULES FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/src/clock.cppm)

gentest_add_mocks(acme_clock_module_gentest_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/clock.cppm
        ${CMAKE_CURRENT_SOURCE_DIR}/test_support/clock_mock_defs.cppm
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gentest/clock_module_mocks"
    LINK_LIBRARIES acme_clock_module
    MODULE_NAME acme.clock.mock.gentest)

install(TARGETS acme_clock_module
    EXPORT acme_clockTargets
    FILE_SET api DESTINATION include)

install(TARGETS acme_clock_module_gentest_mocks
    EXPORT acme_clockTargets
    ARCHIVE DESTINATION lib
    FILE_SET gentest_explicit_mock_module_headers DESTINATION include
    FILE_SET gentest_explicit_mock_modules_private DESTINATION include
    FILE_SET gentest_explicit_mock_aggregate_module DESTINATION include)
```

### Module consumer

```cmake
find_package(acme_clock CONFIG REQUIRED)

add_executable(clock_module_consumer_tests
    main.cpp
    clock_cases.cppm)

target_link_libraries(clock_module_consumer_tests PRIVATE
    gentest::gentest_main
    acme::clock_module_gentest_mocks)

gentest_attach_codegen(clock_module_consumer_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gentest_codegen")
```

```cpp
import acme.clock.mock.gentest;

[[using gentest: test("consumer/clock/module_mock")]]
void uses_exported_module_mock() {
    acme::mocks::ClockMock mock;
    (void)mock;
}
```

## Relationship To `target_install_package`

This story is intentionally aligned with `target_install_package`.

The intended downstream flow is:

1. a library repository generates and exports its own gentest mock targets
2. `target_install_package` installs the normal API and the mock package side
   by side
3. downstream consumers use `find_package(...)` and link the exported mock
   target directly

That keeps the mock surface:

- versioned with the library
- regenerated inside the owning repo
- installable with the rest of the library dev package

## Future Backend Direction

The install layout and target naming should anticipate other backend families.

Possible future targets:

- `acme::clock_gentest_mocks`
- `acme::clock_gmock_mocks`
- `acme::clock_trompeloeil_mocks`

This story does not require multi-backend support yet. It only requires that
the gentest mock-package layout not block that direction.

## Required Test Plan

When this story is implemented for real, add two producer/consumer package
fixtures:

- a textual API library that exports `acme::clock_gentest_mocks`
- a module API library that exports `acme::clock_module_gentest_mocks`

Each fixture should prove:

- the producer installs the generated mock artifacts into a stable library-owned
  include path
- the exported target carries the right include/module file sets
- a downstream consumer can link only the exported mock target and use the mock
  surface without knowing the original defs files
- `target_install_package` preserves the mock-package shape in the installed
  dev package

The existing
[`explicit_mock_target_install_export`](../../tests/cmake/explicit_mock_target_install_export)
fixture is the right starting point, but this story requires splitting it into
two clearly library-owned producer/consumer shapes instead of one generic
fixture-owned export.
