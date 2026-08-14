# gentest modules guide

This page is the module-first counterpart to the root [README](../README.md). It focuses on `import gentest;`, named-module test sources, and explicit mock targets for module-defined types.

## What is supported

Public named modules:
- `import gentest;`
- `import gentest.mock;`
- `import gentest.bench_util;`

`import gentest;` also exports `gentest::format_value`; see [Formatting values](value_formatting.md) for its diagnostic-compatible
fallback order.

Supported module-authored test flows:
- tests, benches, and jitters in named modules
- suite/global fixtures declared in named modules
- explicit mock targets for types defined in headers or named modules
- package-consumer builds that use the installed public modules

## Requirements

- CMake >= 3.31
- C++20 compiler with named-module support
- LLVM/Clang available for `gentest_codegen`
- `clang-scan-deps` discoverable by CMake for module dependency scanning
- Ninja >= 1.11 for module dependency scanning
- Configure the gentest package/provider build with `-DGENTEST_ENABLE_PUBLIC_MODULES=ON` when you want the installed public module surface.

`GENTEST_ENABLE_PUBLIC_MODULES=AUTO` is useful for broad CI matrices because unsupported toolchains quietly disable the public
module surface. For local module work, prefer `ON`: configure fails immediately with the detected reason if CMake cannot build
the public module file set.

On macOS, AppleClang does not provide the LLVM `clang-scan-deps` flow expected by this package. Use Homebrew LLVM for module
builds, set `LLVM_DIR` and `Clang_DIR` to the Homebrew LLVM CMake package directories, and use Ninja >= 1.11. See
[install/macos.md](install/macos.md) for full commands.

For host-toolchain vs target-sysroot setup details, including cross-build
examples, see [buildsystems/host_toolchain_sysroots.md](buildsystems/host_toolchain_sysroots.md).

## CMake quick start

```cmake
include(CTest)
enable_testing()

# Public modules are opt-in on the gentest provider/package build.
# If you build gentest from source in this project, configure it with:
#   -DGENTEST_ENABLE_PUBLIC_MODULES=ON

find_package(gentest CONFIG REQUIRED)

add_executable(my_tests main.cpp cases.cpp)

target_sources(my_tests PRIVATE
  FILE_SET module_cases TYPE CXX_MODULES FILES
    cases.cppm)

target_link_libraries(my_tests PRIVATE
  gentest::gentest
  gentest::gentest_runtime)

gentest_attach_codegen(my_tests
  MODULE_REGISTRATION
  FILE_SET module_cases
  OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gentest_codegen")
gentest_discover_tests(my_tests)

# MODULE_REGISTRATION supports both single-config and multi-config generators.
# With multi-config generators, generated artifacts are emitted under a
# configuration subdirectory of `OUTPUT_DIR` (for example `OUTPUT_DIR/Debug`).
```

If you do not provide your own `main()`, link `gentest::gentest_main` instead of `gentest::gentest_runtime`.

Plain `gentest_attach_codegen(my_tests)` is the additive header-declaration path
for ordinary textual targets: authored `.cpp` files stay attached and generated
sources include only annotated headers. For module-authored tests, use
`MODULE_REGISTRATION`: it adds generated ordinary C++ importer translation units
without replacing the authored `.cppm` module interface in the target or compile
database.

The selected primary module interface must export every annotated case and
every fixture type used by a generated adapter. Gentest deliberately does not
register module-internal entities. For example:

```cpp
export module my.tests;
import gentest;

export namespace demo {

struct Fixture : gentest::FixtureSetup {
    void setUp() override { value = 7; }
    int value = 0;
};

[[using gentest: test("demo/exported")]]
void exported_case(Fixture& fixture) {
    gentest::asserts::EXPECT_EQ(fixture.value, 7);
}

} // namespace demo
```

Codegen produces an ordinary source with this shape:

```cpp
#include <gentest/detail/generated_runtime.h>
import my.tests;
#include "tu_0000_cases.gentest.h" // emitted without a second preamble
```

An annotated exported non-template declaration may be declaration-only. Its
definition can be inline, appear later in the same primary interface, or live
in a normally compiled module implementation source such as `cases.cpp`. A
function template defined only in an implementation unit also needs explicit
instantiation definitions for every generated `template(...)` binding; keeping
the template definition in the exported interface is usually simpler. Only the
primary interface belongs in the `FILE_SET` selected by
`MODULE_REGISTRATION`; add the implementation source to the target normally.
A case or fixture with no exported redeclaration fails codegen with an
importer-reachability diagnostic. Module partitions, private module fragments,
and implementation units are not annotation owners in this
importer-registration surface.

GCC 16.1 has a compiler bug when an ordinary importer loads an exported inline
coroutine definition from a named module. For that compiler, keep the annotated
coroutine declaration in the exported interface and put its body in a normally
compiled module implementation source. This is the same portable two-unit form
shown above; Clang and earlier supported GCC releases also accept it.

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

## Assertions and exceptions

Include-based consumers can use gtest-like macros. Module consumers should use the matching function templates from
`gentest::asserts`, or the lowercase top-level `gentest::expect_*` / `gentest::require_*` helpers.

| Top-level `gentest::` | `gentest::asserts::` |
|---|---|
| `expect_true` / `expect` | `EXPECT_TRUE` |
| `expect_false` | `EXPECT_FALSE` |
| `expect_eq` | `EXPECT_EQ` |
| `expect_ne` | `EXPECT_NE` |
| `expect_lt` | `EXPECT_LT` |
| `expect_le` | `EXPECT_LE` |
| `expect_gt` | `EXPECT_GT` |
| `expect_ge` | `EXPECT_GE` |
| `require` | `ASSERT_TRUE` |
| `require_false` | `ASSERT_FALSE` |
| `require_eq` | `ASSERT_EQ` |
| `require_ne` | `ASSERT_NE` |

```cpp
import gentest;
using namespace gentest::asserts;

[[gentest::test("exceptions/module_functions")]]
void module_functions() {
    EXPECT_THROW<std::runtime_error>([] { throw std::runtime_error("boom"); });
    EXPECT_NO_THROW([] {});
}
```

For richer checks, use normal C++ plus `gentest::fail(...)`, `gentest::expect_true(...)`, or `gentest::expect(...)`.

## Module-local mocks

Use an explicit mock target and import its generated module surface:

```cpp
import my.service_mocks;

demo::mocks::ServiceMock service;
gentest::expect(service, &demo::Service::compute).times(1).with(3).returns(9);
```

After the generated surface is visible, raw `gentest::mock<demo::Service>` is also valid.

Do not put a direct `gentest::mock<T>` fixture parameter in a
`MODULE_REGISTRATION` interface. Generated mock attachment specializations
cannot be added from the unrelated importer TU. Publish an explicit mock target,
import its generated module surface, and construct the exported mock type inside
the test body as shown above.

## Practical notes

- `gentest_codegen` uses your active compilation database. If module imports fail during codegen, check the target's `compile_commands.json` first.
- `import gentest;` consumers should link `gentest::gentest`, not just `gentest::gentest_main`.
- If you need background on how the module flow was built and hardened, see:
  - [docs/stories/009_public_modules_end_to_end.md](stories/009_public_modules_end_to_end.md)
  - [docs/stories/010_public_modules_progress_report.md](stories/010_public_modules_progress_report.md)
