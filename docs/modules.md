# gentest modules guide

This page is the module-first counterpart to the root [README](../README.md). It focuses on `import gentest;`, named-module test sources, and explicit mock targets for module-defined types.

## What is supported

Public named modules:
- `import gentest;`
- `import gentest.mock;`
- `import gentest.bench_util;`

Supported module-authored test flows:
- tests, benches, and jitters in named modules
- suite/global fixtures declared in named modules
- explicit mock targets for types defined in headers or named modules
- package-consumer builds that use the installed public modules

## Requirements

- CMake >= 3.31
- C++20 compiler with named-module support
- LLVM/Clang available for `gentest_codegen`
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON`
- installed-package consumers also need the exact matching `fmt` CMake package
  discoverable, typically through the same `CMAKE_PREFIX_PATH` as `gentest`

For host-toolchain vs target-sysroot setup details, including cross-build
examples, see [buildsystems/host_toolchain_sysroots.md](buildsystems/host_toolchain_sysroots.md).

Per-TU wrapper mode for module sources requires a single-config generator/build directory. For multi-config generators, use manifest mode with `gentest_attach_codegen(... OUTPUT ...)`.

## CMake quick start

```cmake
include(CTest)
enable_testing()

find_package(gentest CONFIG REQUIRED)

add_executable(my_tests
  main.cpp
  cases.cppm)

target_link_libraries(my_tests PRIVATE
  gentest::gentest
  gentest::gentest_runtime)

gentest_attach_codegen(my_tests)
gentest_discover_tests(my_tests)
```

If you do not provide your own `main()`, link `gentest::gentest_main` instead of `gentest::gentest_runtime`.

## Minimal layout

`main.cpp`:

```cpp
import gentest;
import my.tests;

auto main(int argc, char** argv) -> int {
    return gentest::run_all_tests(argc, argv);
}
```

`service.cppm`:

```cpp
export module my.service;

export namespace demo {

struct Service {
    virtual ~Service() = default;
    virtual int compute(int arg) = 0;
};

} // namespace demo
```

`mock_defs.cppm`:

```cpp
export module my.mock_defs;

import gentest.mock;

export import my.service;

export namespace demo::mocks {

using ServiceMock = gentest::mock<demo::Service>;

} // namespace demo::mocks
```

`cases.cppm`:

```cpp
export module my.tests;

import gentest;
import gentest.bench_util;
import my.service_mocks;

using namespace gentest::asserts;

export namespace demo {

struct [[using gentest: fixture(suite)]] SuiteFixture : gentest::FixtureSetup {
    void setUp() override { value = 7; }
    int value = 0;
};

[[using gentest: test("demo/module_test")]]
void module_test(SuiteFixture& suite_fx) {
    EXPECT_EQ(suite_fx.value, 7);
}

[[using gentest: test("demo/module_mock")]]
void module_mock() {
    demo::mocks::ServiceMock mock_service;
    gentest::expect(mock_service, &Service::compute).times(1).with(3).returns(9);
    Service* service = &mock_service;
    EXPECT_EQ(service->compute(3), 9);
}

[[using gentest: bench("demo/module_bench"), baseline]]
void module_bench(SuiteFixture& suite_fx) {
    gentest::doNotOptimizeAway(suite_fx.value);
}

} // namespace demo
```

Add the mock target in CMake:

```cmake
gentest_add_mocks(my_service_mocks
  DEFS
    ${CMAKE_CURRENT_SOURCE_DIR}/service.cppm
    ${CMAKE_CURRENT_SOURCE_DIR}/mock_defs.cppm
  OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/mocks"
  MODULE_NAME my.service_mocks)

add_executable(my_tests
  main.cpp
  cases.cppm)

target_link_libraries(my_tests PRIVATE
  gentest::gentest
  gentest::gentest_runtime
  my_service_mocks)

gentest_attach_codegen(my_tests)
gentest_discover_tests(my_tests)
```

Link explicit mock targets before `gentest_attach_codegen()`, so codegen sees
the generated mock surface during its first parse.

The important rule is simple:

- header defs stay header-based and publish a generated header
- module defs stay module-based and publish a generated module
- module test sources do not need `#if defined(GENTEST_CODEGEN)` bootstrap blocks for direct mocks anymore

## Assertions and exceptions

Include-based consumers can use gtest-like macros. Module consumers should use the matching function templates from `gentest::asserts`.

```cpp
import gentest;
using namespace gentest::asserts;

[[gentest::test("exceptions/module_functions")]]
void module_functions() {
    EXPECT_THROW<std::runtime_error>([] { throw std::runtime_error("boom"); });
    EXPECT_NO_THROW([] {});
}
```

For richer checks, use normal C++ plus `gentest::fail(...)` or `gentest::expect(...)`.

## Module-local mocks

Use an explicit mock target and import its generated module surface:

```cpp
import my.service_mocks;

demo::mocks::ServiceMock service;
gentest::expect(service, &demo::Service::compute).times(1).with(3).returns(9);
```

After the generated surface is visible, raw `gentest::mock<demo::Service>` is also valid.

## Practical notes

- `gentest_codegen` uses your active compilation database. If module imports fail during codegen, check the target's `compile_commands.json` first.
- `import gentest;` consumers should link `gentest::gentest`, not just `gentest::gentest_main`.
- If you need background on how the module flow was built and hardened, see:
  - [docs/stories/009_public_modules_end_to_end.md](stories/009_public_modules_end_to_end.md)
  - [docs/stories/010_public_modules_progress_report.md](stories/010_public_modules_progress_report.md)
