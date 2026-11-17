# Named modules

This page shows how to write gentest tests as C++20 named modules: use gentest's
API with `import gentest;`, register cases that you declare in module interface
units, and mock types that your modules define.

Each test target uses one of two ways to register cases:

- **Header registration** (the default): annotated declarations live in headers,
  and the test code uses gentest with `#include`. See
  [Writing tests](writing_tests.md).
- **Module registration** (`MODULE_REGISTRATION`, this page): cases live in
  named-module interface units, and the test code uses `import gentest;`.

A header registration target can't contain named-module sources or import a
named module.

## Requirements

Module registration needs a gentest install that includes gentest's module
interfaces, and a toolchain that CMake can scan for module dependencies.

| Component | Requirement |
|---|---|
| CMake | 3.31 or newer. |
| Generator | Ninja or Ninja Multi-Config, with Ninja 1.11 or newer, for gentest and for your project. |
| GCC | 15 or newer. 16 or newer when you build with `-fsanitize=thread`. |
| Clang | 16 or newer (19.1 or newer for `clang-cl`), with a matching `clang-scan-deps`. CMake looks for it next to the compiler and on `PATH`; set `CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS` if it isn't found. |
| AppleClang | Not supported: CMake has no module dependency scanning for AppleClang (as of CMake 4.5). Use Homebrew LLVM, as [Install gentest on macOS](install/macos.md) shows. |
| MSVC | 19.34 (Visual Studio 2022 17.4) or newer, the oldest version that CMake scans for modules. |
| `gentest_codegen` | A `clang-scan-deps` executable, whichever compiler builds your code. Codegen uses it for every module registration target and every module mock target. CMake looks for it by itself, or set `GENTEST_CODEGEN_CLANG_SCAN_DEPS`; [Host toolchains and cross-compiling](buildsystems/host_toolchain_sysroots.md) lists where it looks. |

For a separate host toolchain or a cross build, see
[Host toolchains and cross-compiling](buildsystems/host_toolchain_sysroots.md).

### Build gentest with its module interfaces

The `GENTEST_ENABLE_PUBLIC_MODULES` option of the gentest build decides whether
gentest builds and installs its modules `gentest`, `gentest.mock` and
`gentest.bench_util`:

| Value | Effect |
|---|---|
| `OFF` (default) | No module interfaces. `import gentest;` fails in your project. |
| `ON` | Builds and installs them. If the toolchain can't build them, configure stops with `GENTEST_ENABLE_PUBLIC_MODULES=ON was requested, but the current toolchain cannot support gentest's public modules: <reason>`. |
| `AUTO` | Builds them if the toolchain can. Otherwise configure continues without them and prints `gentest: public named modules disabled automatically: <reason>`. |

Add `-DGENTEST_ENABLE_PUBLIC_MODULES=ON` to the gentest configure command on the
install page for your platform ([Linux](install/linux.md),
[macOS](install/macos.md) or [Windows](install/windows.md)), or to your
project's configure command if it builds gentest with `add_subdirectory()` or
`FetchContent`. `GENTEST_ENABLE_PUBLIC_MODULES=AUTO` suits CI matrices that mix
toolchains. An install with
modules contains `include/gentest/gentest.cppm`.

## Quick start

This project builds a test executable from one module interface unit.

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.31)
project(my_tests LANGUAGES CXX)

include(CTest)

# Compile as -std=c++20, like gentest's module interfaces.
# Clang can't import them into gnu++20 code.
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(gentest CONFIG REQUIRED)

add_executable(my_tests main.cpp)
target_compile_features(my_tests PRIVATE cxx_std_20)
target_sources(my_tests PRIVATE
  FILE_SET module_cases TYPE CXX_MODULES FILES
    cases.cppm)
target_link_libraries(my_tests PRIVATE gentest::gentest gentest::gentest_runtime)

gentest_attach_codegen(my_tests MODULE_REGISTRATION FILE_SET module_cases)
gentest_discover_tests(my_tests)
```

`cases.cppm`:

```cpp
export module my.tests;

import gentest;

using namespace gentest::asserts;

export namespace demo {

struct Counter : gentest::FixtureSetup {
    void setUp() override { value = 7; }
    int value = 0;
};

[[using gentest: test("addition")]]
void addition() {
    gentest::expect_true(2 + 2 == 4, "basic arithmetic");
    EXPECT_EQ(2 + 2, 4);
}

[[using gentest: test("fixture")]]
void uses_fixture(Counter& counter) {
    EXPECT_EQ(counter.value, 7);
}

} // namespace demo
```

`main.cpp`:

```cpp
import gentest;

auto main(int argc, char** argv) -> int {
    return gentest::run_all_tests(argc, argv);
}
```

Configure, build and run it against your gentest install:

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build
ctest --test-dir build --output-on-failure
```

CTest runs `demo/addition` and `demo/fixture`. On macOS, also pass
`-DCMAKE_CXX_COMPILER` with Homebrew LLVM's `clang++`, as
[Install gentest on macOS](install/macos.md) shows; CMake finds the
`clang-scan-deps` beside it.

What each part does:

- `MODULE_REGISTRATION FILE_SET module_cases` tells codegen to register the
  cases in the interface units of the `CXX_MODULES` file set `module_cases`.
- The case and the fixture are exported, because codegen can only register what
  the module exports; see
  [Export what codegen registers](#export-what-codegen-registers).
- `gentest::gentest` owns the module interfaces, so link it whenever your code
  uses `import gentest;`. Linking only `gentest::gentest_main` is not enough.
- `main.cpp` defines `main()`, so the target links `gentest::gentest_runtime`.
  To let gentest provide `main()`, remove `main.cpp` and link
  `gentest::gentest_main` instead, as
  [examples/hello_modules](../examples/hello_modules/) does.
- `CMAKE_CXX_EXTENSIONS OFF` compiles your code as `-std=c++20`, the dialect of
  gentest's module interfaces. With Clang, a `gnu++20` build stops at the
  codegen step.
- `gentest_discover_tests()` adds each case to CTest; see
  [CTest integration](discover_tests.md).

## Write cases in a module

### Export what codegen registers

Codegen registers the cases from a generated source file that imports your
module, so it can use only what the module exports
([How module registration works](#how-module-registration-works) shows that
file). Export:

- every annotated case. Put it in an `export namespace`, in an `export { ... }`
  block, or start its declaration with `export`.
- every fixture type that a case takes as a parameter, including suite and
  global fixtures. An exported alias of a type that isn't exported is enough.
  Codegen rejects a type from the global module fragment used directly as a
  fixture type.
- every name used in `template(...)` arguments and in `parameters(...)` values.

Codegen doesn't skip what you forgot to export: it names each declaration and
stops.

### Where definitions can live

The interface unit must declare each case, but the definition can be:

- at the declaration, as in the quick start;
- later in the same interface unit;
- in a module implementation unit (`module my.tests;`) that the target compiles
  as an ordinary source. Don't add it to the `FILE_SET`.

A function template defined in an implementation unit needs an explicit
instantiation for every `template(...)` argument, because the generated source
can't see its body. Keeping template definitions in the interface avoids this.

This interface declares three cases and an implementation unit defines them:

`cases.cppm`:

```cpp
export module my.tests;

import gentest;

export namespace demo {

struct Counter : gentest::FixtureSetup {
    void setUp() override { value = 7; }
    int value = 0;
};

[[using gentest: test("declared_here")]]
void declared_here(Counter& counter);

template <typename T>
[[using gentest: test("sizes"), template(T, int, long)]]
void sizes();

[[using gentest: test("async")]]
gentest::awaitable<void> async_case();

} // namespace demo
```

`cases_impl.cpp`:

```cpp
module;

#include <coroutine>
#include <memory>

module my.tests;

import gentest;

namespace demo {

void declared_here(Counter& counter) {
    gentest::asserts::EXPECT_EQ(counter.value, 7);
}

template <typename T>
void sizes() {
    gentest::asserts::EXPECT_TRUE(sizeof(T) >= sizeof(int));
}

template void sizes<int>();
template void sizes<long>();

gentest::awaitable<void> async_case() {
    co_return;
}

} // namespace demo
```

Add the implementation unit as an ordinary source:

```cmake
add_executable(my_tests main.cpp cases_impl.cpp)
```

### What the file set can contain

The `FILE_SET` that you pass with `MODULE_REGISTRATION` may contain only primary
module interface units (`export module name;`). Codegen reads annotations only
from these files and writes one registration source for each, so one file set
can hold several modules. Codegen stops with an error for:

- a module implementation unit (`module name;`);
- a module partition (`export module name:part;`);
- a unit with a private module fragment (`module :private;`).

Declare cases in the primary interface. Put partitions in a second `CXX_MODULES`
file set of the same target, and re-export them from the primary interface with
`export import :part;`:

```cmake
target_sources(my_tests PRIVATE
  FILE_SET module_cases TYPE CXX_MODULES FILES cases.cppm
  FILE_SET module_parts TYPE CXX_MODULES FILES helpers.cppm)

gentest_attach_codegen(my_tests MODULE_REGISTRATION FILE_SET module_cases)
```

### Coroutine cases

[Async tests](async.md) work in modules too. Two rules apply:

- The unit that defines a coroutine case must include `<coroutine>` in its
  global module fragment, because `import gentest;` doesn't provide it. With
  GCC, include `<memory>` there as well.
- GCC 16.1 and 16.2 fail to load a coroutine definition from a module interface
  in the generated registration source. Declare coroutine cases in the interface
  and define them in an implementation unit, as `async_case` above does. Clang
  accepts both forms.

## Use the gentest modules

| Module | Contents | Header counterpart |
|---|---|---|
| `gentest` | Assertions, the matchers in `gentest::match`, `gentest::approx::Approx`, fixture base classes, skip and xfail, logging, the test context, notifications, records and properties, async types, the benchmark measurement API, `gentest::format_value` and the runner entry points such as `gentest::run_all_tests`. | `gentest/runner.h`, `gentest/benchmark.h`, `gentest/record.h` |
| `gentest.mock` | `gentest::mock`, `gentest::expect` for mocks, `gentest::make_nice`, `gentest::make_strict`, and the matchers in `gentest::match` again. | `gentest/mock.h` |
| `gentest.bench_util` | `gentest::doNotOptimizeAway` and `gentest::clobberMemory`. | `gentest/bench_util.h` |

Suite and global fixtures, benchmarks and jitter cases work in modules as they
do in headers; see [Fixtures](fixtures.md) and [Benchmarks](benchmarks.md).

### Assertions and other macros

Modules export no macros. The `EXPECT_*` and `ASSERT_*` names in
`gentest::asserts` are functions, so they work the same after `import gentest;`,
except for these macros, which have function forms:

| With `#include` (macro) | With `import` (function) |
|---|---|
| `EXPECT_THROW(statement, E)`, `ASSERT_THROW(statement, E)` | `EXPECT_THROW<E>(callable)`, `ASSERT_THROW<E>(callable)` |
| `EXPECT_NO_THROW(statement)`, `ASSERT_NO_THROW(statement)` | `EXPECT_NO_THROW(callable)`, `ASSERT_NO_THROW(callable)` |
| `EXPECT_CALL(mock, method)`, `ASSERT_CALL(mock, method)` | `gentest::expect(mock, &Type::method)` |

[Assertions](assertions.md) shows both forms of the exception checks and lists
the lowercase names, such as `gentest::expect_eq` and `gentest::require`, which
work the same after `import gentest;`. The opt-in macro headers
`gentest/analyzer_assertions.h` and `gentest/assert_libassert.h` have no module
form.

## Mock types from modules

To mock a type that a named module defines, give `gentest_add_mocks()` module
`DEFS`. The resulting mock target generates a module that your tests import.
[Mocks](mock_generation.md) covers expectations, matchers and mocks of
header-defined types.

The example extends the quick start with a `mocks` directory:

```text
CMakeLists.txt
main.cpp
cases.cppm
mocks/service.cppm
mocks/mock_defs.cppm
```

`mocks/service.cppm` defines the type to mock:

```cpp
export module my.service;

export namespace demo {

struct Service {
    virtual ~Service() = default;
    virtual int compute(int arg) = 0;
};

} // namespace demo
```

`mocks/mock_defs.cppm` is the mock definitions module. It names each mock with
`gentest::mock<T>`:

```cpp
export module my.mock_defs;

import gentest.mock;
export import my.service;

export namespace demo::mocks {

using ServiceMock = gentest::mock<demo::Service>;

} // namespace demo::mocks
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.31)
project(my_tests LANGUAGES CXX)

include(CTest)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(gentest CONFIG REQUIRED)

gentest_add_mocks(my_service_mocks
  DEFS
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/service.cppm
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/mock_defs.cppm
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/mocks
  MODULE_NAME my.service_mocks)

add_executable(my_tests main.cpp)
target_compile_features(my_tests PRIVATE cxx_std_20)
target_sources(my_tests PRIVATE
  FILE_SET module_cases TYPE CXX_MODULES FILES
    cases.cppm)

# Link mock targets before gentest_attach_codegen().
target_link_libraries(my_tests PRIVATE gentest::gentest gentest::gentest_runtime my_service_mocks)

gentest_attach_codegen(my_tests MODULE_REGISTRATION FILE_SET module_cases)
gentest_discover_tests(my_tests)
```

`cases.cppm`:

```cpp
module;

#include <typeinfo> // GCC needs it where mock expectations are set.

export module my.tests;

import gentest;
import my.service_mocks;

using namespace gentest::asserts;

export namespace demo {

[[using gentest: test("mock")]]
void uses_mock() {
    demo::mocks::ServiceMock service;
    gentest::expect(service, &demo::Service::compute).times(1).with(3).returns(9);

    demo::Service& base = service;
    EXPECT_EQ(base.compute(3), 9);
}

} // namespace demo
```

`main.cpp` is the one from the quick start.

`MODULE_NAME my.service_mocks` names the module that the mock target
generates. It re-exports `gentest`, `gentest.mock` and every `DEFS` module, so
one `import my.service_mocks;` gives a test the mock, the mocked type and the
mock API. As with header mocks, link the mock target before you call
`gentest_attach_codegen()`, or link it with
[`gentest_link_mocks()`](mock_generation.md#link-mock-targets), which also
works after that call.

### Rules for module mocks

- Define each mocked type at namespace scope in a module interface unit that
  you list in `DEFS`. Nested types are rejected, and so is a type defined in a
  header that the module includes after its `export module` line.
- Get the `DEFS` modules only from the mock target. If another target linked
  into the test also builds `my.service`, the test sees two different modules
  with that name and the build fails.
- Create mocks in the test body. In a module that `MODULE_REGISTRATION`
  registers, don't take a mock as a case parameter, and don't name
  `gentest::mock<T>` for a type that the module itself defines. Codegen rejects
  both.
- A module-registered test can't use a mock target built from header `DEFS`.
  Mock header-defined types in header-registered tests instead.
- With GCC, every module that sets mock expectations needs `#include <typeinfo>`
  in its global module fragment, as in `cases.cppm` above.

### Install a module mock target

A mock target built from module `DEFS` publishes three file sets. Install all of
them with the target:

```cmake
include(GNUInstallDirs)

install(TARGETS my_service_mocks
  EXPORT my_service_mocksTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  FILE_SET gentest_explicit_mock_module_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILE_SET gentest_explicit_mock_modules_private DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILE_SET gentest_explicit_mock_aggregate_module DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT my_service_mocksTargets
  NAMESPACE app::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/my_service_mocks)
```

The installed module units are the generated copies of your `DEFS`, with their
global module fragments unchanged, so also install the headers those fragments
include. The package configuration file must find gentest first with
`find_dependency(gentest CONFIG REQUIRED)`. [Mocks](mock_generation.md) shows
the rest of the packaging steps.

## CMake reference

### `gentest_attach_codegen()` with `MODULE_REGISTRATION`

```cmake
gentest_attach_codegen(<target>
  MODULE_REGISTRATION
  FILE_SET <name>
  [OUTPUT_DIR <dir>]
  [CLANG_ARGS <arg>...]
  [DEPENDS <dependency>...]
  [QUIET_CLANG]
  [STRICT_FIXTURE])
```

- `FILE_SET <name>` is required with `MODULE_REGISTRATION` and valid only with
  it. It names a `CXX_MODULES` file set of `<target>` with at least one file.
  `SOURCES` is rejected.
- `OUTPUT_DIR` defaults to `${CMAKE_CURRENT_BINARY_DIR}/gentest/<target>_<hash>`.
  It must be a concrete path without generator expressions, and each target
  needs its own. Multi-config generators write to a subdirectory per
  configuration, such as `<OUTPUT_DIR>/Debug`.
- `CLANG_ARGS` adds Clang arguments for the codegen parse only, not for the
  compiler.
- `DEPENDS` names targets or files that codegen waits for, such as generated
  headers.
- `QUIET_CLANG` hides Clang warnings and remarks during the codegen parse.
  Errors print and fail the build as usual.
- `STRICT_FIXTURE` makes codegen reject member tests on suite and global
  fixtures.

### `gentest_add_mocks()` with module `DEFS`

- A `DEFS` file is a module when its last extension is `.cppm`, `.ccm`,
  `.cxxm`, `.ixx` or `.mxx`; any other file is a mock definitions header.
- Every `DEFS` module must be a primary module interface unit: partitions,
  implementation units and private module fragments are rejected. At least one
  of them must define a mocked type.
- The generated module is
  `<OUTPUT_DIR>/<MODULE_NAME with dots replaced by slashes>.cppm`, for example
  `my/service_mocks.cppm`.

The other arguments, including the `MODULE_NAME`, `HEADER_NAME` and `BACKEND`
rules for module `DEFS`, are in the
[`gentest_add_mocks()` reference](mock_generation.md#gentest_add_mocks-reference).

## Other build systems

This page uses CMake. Bazel (`gentest_attach_codegen_modules`,
`gentest_add_mocks_modules`) and Xmake (`kind = "modules"`) also support module
registration and module mocks; Bazel's module rules need Clang as the test
compiler. Meson supports header registration only. See
[Choosing a build system](buildsystems/downstream_consumers.md),
[Bazel](buildsystems/bazel.md) and [Xmake](buildsystems/xmake.md).

## Troubleshooting

| Message or symptom | Fix |
|---|---|
| `GENTEST_ENABLE_PUBLIC_MODULES=ON was requested, but the current toolchain cannot support gentest's public modules: <reason>` (configuring gentest) | Meet the [requirements](#requirements) named in the reason. |
| `fatal error: module 'gentest' not found` (Clang), or another compiler's message that it can't find module `gentest` | Link `gentest::gentest`; linking only `gentest::gentest_main` is not enough. If you do, check that the gentest install contains `include/gentest/gentest.cppm`, and rebuild it with `-DGENTEST_ENABLE_PUBLIC_MODULES=ON` if it doesn't. |
| `gentest_attach_codegen(<target>): named-module sources require explicit importer registration. ...` | Add `MODULE_REGISTRATION FILE_SET <name>` to the call. |
| `header-declaration registration does not support textual sources with active named-module imports; use the MODULE_REGISTRATION path for module-authored tests` | A header registration target imports a named module. Use module registration for that code. |
| `gentest_attach_codegen(<target>): named-module codegen requires clang-scan-deps. ...` | Install `clang-scan-deps` with your LLVM, or set `GENTEST_CODEGEN_CLANG_SCAN_DEPS` to its path. |
| `error: GNU extensions was disabled in precompiled file '...' but is currently enabled` | Set `CMAKE_CXX_EXTENSIONS OFF`, as in the [quick start](#quick-start). |
| `Gentest case is not reachable after importing named module '<module>'; MODULE_REGISTRATION uses an ordinary importer translation unit, so export the declaration` | [Export](#export-what-codegen-registers) the case. |
| `fixture type '<type>' is not reachable after importing named module '<module>'; ...`, or `template binding '...'` / `parameter expression '...'` `names '...' which is not reachable after importing named module '<module>'` | [Export](#export-what-codegen-registers) the fixture type or the named entity, or an alias of it. |
| `module registration input '<file>' is a module implementation unit` / `declares module partition` / `contains a private module fragment` | Keep only primary interface units in the file set; see [What the file set can contain](#what-the-file-set-can-contain). |
| `MODULE_REGISTRATION cannot generate direct mocks owned by named module '<module>'; ...` | Move the mock into a [mock target](#mock-types-from-modules) and create it in the test body. |
| `--mock-registration-manifest does not support header-like mock '<type>' in module registration mode` | A module-registered test uses a mock of a header-defined type; see [Rules for module mocks](#rules-for-module-mocks). |
| GCC: `must ‘#include <typeinfo>’ before using ‘typeid’` | Add `#include <typeinfo>` to the global module fragment of the unit that sets mock expectations. |
| GCC 16: `failed to read compiled module cluster 1: Bad file data` when the generated registration source is compiled | Move coroutine case definitions into an implementation unit; see [Coroutine cases](#coroutine-cases). |
| Codegen fails to import a module that your compiler builds | Codegen reads your module with the host Clang and the flags from the build directory's `compile_commands.json`. Check those flags, and check that Clang accepts the code. |

## How module registration works

For each interface unit in the file set, codegen writes a registration header
`tu_<NNNN>_<name>.gentest.h` and a source
`tu_<NNNN>_<name>.registration.gentest.cpp` to `OUTPUT_DIR`. Shortened, the
source looks like this:

```cpp
#include "gentest/detail/generated_runtime.h" // gentest's registration support
import my.tests;
#include "tu_0000_cases.gentest.h"            // the registrations
```

It never includes your `.cppm` file; it only imports your module, which is why
it can name only exported declarations. Your interface unit stays in the target
and compiles as usual. Codegen parses your module with the host Clang and uses
`clang-scan-deps` to precompile the modules it imports.
[Codegen artifact protocols](codegen_artifact_protocols.md) describes the
manifest codegen writes beside these files, and the
[design history](stories/README.md) has the background.
