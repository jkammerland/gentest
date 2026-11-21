# Meson

This page shows how to use gentest in a Meson project. You add gentest as a
subproject, describe your test executables and mock targets in Meson
dictionaries, and load a helper that runs `gentest_codegen` for them.

Meson supports header registration only (see
[Writing tests](../writing_tests.md)); named modules are not supported. In
gentest's Meson API, `textual` (as in `gentest_textual_suites`) means header
registration. To compare Meson with the other integrations, see
[Choosing a build system](downstream_consumers.md).

## Quick start

You need:

- a gentest source checkout at `subprojects/gentest`;
- a `gentest_codegen` built from that checkout, which needs CMake and the
  LLVM/Clang development packages (see [Install gentest on Linux](../install/linux.md));
- a host Clang for `gentest_codegen` to use. Your test code can use any
  C++20 compiler.

Add gentest and build its code generator with the `host-codegen` CMake preset:

```bash
git clone https://github.com/jkammerland/gentest.git subprojects/gentest
cd subprojects/gentest
cmake --preset=host-codegen
cmake --build --preset=host-codegen
cd ../..
```

The preset writes `subprojects/gentest/build/host-codegen/tools/gentest_codegen`.
That is where gentest's Meson build looks when you don't set `codegen_path`. If
CMake can't find LLVM, pass `-DLLVM_DIR=...` and `-DClang_DIR=...` to the first
`cmake` command.

`meson.build`:

```meson
project('my_tests', 'cpp', default_options: ['cpp_std=c++20'])

gentest_sp = subproject('gentest', default_options: ['build_self_tests=false'])

gentest_textual_suites = [
  {
    'name': 'my_tests',
    'declaration_headers': ['tests/cases.hpp'],
    'source_includes': ['tests'],
  },
]

subdir('subprojects/gentest/meson/textual')
```

`tests/cases.hpp`:

```cpp
#pragma once

#include "gentest/test.h"

using namespace gentest::asserts;

namespace demo {

[[gentest::test]]
inline void addition() {
    EXPECT_EQ(2 + 2, 4);
}

} // namespace demo
```

Build and run:

```bash
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
build/subprojects/gentest/meson/textual/my_tests --list
```

`meson test` runs each test executable as one Meson test. The helper builds its
targets in `build/subprojects/gentest/meson/textual/`; run an executable from
that directory directly to pass runner options such as `--list` or
`--run=demo/addition` (see [Command-line options](../command_line.md)).

## How the helper works

Meson has no user-defined functions, so gentest's Meson API is a `subdir()`
fragment. Before `subdir('subprojects/gentest/meson/textual')`, your
`meson.build` defines these variables:

| Variable | Contents |
| --- | --- |
| `gentest_sp` | The result of `subproject('gentest', ...)`. The name is fixed: the helper reads gentest's variables from it. |
| `gentest_textual_suites` | A list with one dictionary per test executable. Optional. |
| `gentest_textual_mocks` | A list with one dictionary per mock target. Optional. |

Without `gentest_sp`, configuration stops with:

```text
gentest Meson textual helper requires gentest_sp = subproject('gentest', ...) before subdir('subprojects/gentest/meson/textual')
```

Rules for the dictionaries:

- Call `subdir()` once, after you have defined every entry. Meson doesn't enter
  the same directory twice.
- Paths in `source`, `declaration_headers`, `defs` and `source_includes` are
  strings, relative to your project's source root or absolute.
- Pass `main` and other extra sources as `files()` objects created in your own
  `meson.build`, for example `files('tests/main.cpp')`. The helper runs in a
  directory of the gentest subproject, so a plain string would name a file
  there.
- All generated files land in one directory,
  `<builddir>/subprojects/gentest/meson/textual/`. Give each entry its own
  `name` (or `id`) and each mock its own `public_header`.

## gentest's Meson options

| Option | Default | Meaning |
| --- | --- | --- |
| `codegen_path` | empty | Path to `gentest_codegen`. Empty means `build/host-codegen/tools/gentest_codegen` inside the gentest subproject (`gentest_codegen.exe` on Windows). A relative path is resolved against the gentest subproject's directory, so prefer an absolute path. |
| `codegen_host_clang` | empty | Host Clang for `gentest_codegen`, passed as `--host-clang`. Empty lets `gentest_codegen` find one; [Host toolchains and cross-compiling](host_toolchain_sysroots.md) lists the order. |
| `build_self_tests` | `true` | Builds gentest's own Meson tests. Set it to `false` in your project. |

Set them in the `default_options` of your `subproject()` call, or on the
command line with the subproject prefix, for example
`meson setup build -Dgentest:codegen_path=/opt/gentest/bin/gentest_codegen`.

```meson
gentest_sp = subproject(
  'gentest',
  default_options: [
    'build_self_tests=false',
    'codegen_path=/opt/gentest/bin/gentest_codegen',
    'codegen_host_clang=/usr/lib/llvm-23/bin/clang++',
  ],
)
```

Use a `gentest_codegen` built from the same gentest version as the subproject.
To build for another target, see
[Host toolchains and cross-compiling](host_toolchain_sysroots.md). The options
`gentest_codegen_path` and `gentest_codegen_host_clang` in
[`tests/downstream/meson_wrap_consumer`](../../tests/downstream/meson_wrap_consumer)
are that example's own, forwarded to the two options above.

### fmt

gentest's Meson build compiles fmt header-only (`FMT_HEADER_ONLY`). It takes
the fmt include directory from pkg-config (`fmt`), else from the fmt sources
that the `host-codegen` preset fetched (`_deps/fmt-src/include` next to
`tools/`), else from the compiler's default include path.

## Test executables

Each dictionary in `gentest_textual_suites` creates one executable. It needs
`source`, `declaration_headers`, or both:

- **Header-only.** List the annotated headers in `declaration_headers` and
  leave out `source`. Codegen scans each header on its own.
- **Out-of-line definitions.** Also pass one `source` file, which must include
  the annotated headers. It is the only file codegen scans; keep the headers in
  `declaration_headers` so editing them reruns codegen.

For example, a test executable with its definitions in `tests/cases.cpp` and
its own `main()`:

```meson
gentest_textual_suites = [
  {
    'name': 'service_tests',
    'source': 'tests/cases.cpp',
    'declaration_headers': ['tests/cases.hpp'],
    'main': files('tests/main.cpp'),
    'source_includes': ['tests'],
  },
]
```

| Field | Default | Meaning |
| --- | --- | --- |
| `name` | required | Executable name, and the key in the lookup dictionaries. |
| `id` | `name` | Used in generated file names and in the codegen target name `gen_<id>`. |
| `kind` | `'textual'` | Any other value stops configuration. |
| `declaration_headers` | `[]` | Annotated headers. |
| `source` | none | One `.cpp` file with out-of-line definitions. |
| `main` | none | Extra sources as `files()` objects, such as your own `main()`. Without them, the executable takes `main()` from gentest's `gentest_main` library. |
| `source_includes` | `[]` | Include directories for codegen and for the compiler. |
| `mocks` | `[]` | Names of entries in `gentest_textual_mocks` to use. |
| `clang_args` | `[]` | Extra arguments for codegen only. |
| `cpp_args` | `[]` | Extra arguments for the compiler only. |
| `include_directories` | `[]` | `include_directories()` objects for the compiler. Codegen doesn't see them. |
| `dependencies` | `[]` | Meson dependencies for the executable. Codegen doesn't see their include paths. |
| `link_with` | `[]` | Libraries to link. |
| `test` | `true` | Registers the executable as a Meson test. |
| `test_name` | `name` | Name of that Meson test. |
| `death_tests` | `[]` | One dictionary per [death test](../death_tests.md) to register separately. |
| `install` | `false` | Installs the executable. |
| `build_by_default` | `true` | Applies to the codegen step only; the executable itself is built by default either way. |

Codegen doesn't use a compile database in Meson builds. It parses with
`-std=c++20`, `-DGENTEST_CODEGEN=1`, `-DFMT_HEADER_ONLY`, gentest's include
directories, the helper's build directory and `source_includes`. If your
headers need other include paths or definitions, pass them through
`source_includes` (both codegen and the compiler) or `clang_args` (codegen
only).

The executable links gentest through `gentest_main_dep` and
`gentest_runtime_dep`, so it doesn't need a `dependencies` entry for gentest.

### Death tests

List each case by its full Gentest name in the suite dictionary.

```meson
'death_tests': [
  {'case': 'crash/fatal_input', 'name': 'fatal_input', 'expect': 'invalid input'},
],
```

`meson test -C build fatal_input` runs the case under `gentest_supervise`.
A runtime skip appears as `SKIP`. The default Meson test name is
`<suite>/death/<case>`. Each entry accepts `timeout_ms` and `inactivity_ms`
for the supervisor, `args` for the test executable, and `timeout` in seconds
for Meson's outer timeout (default 30).

## Mocks

Each dictionary in `gentest_textual_mocks` creates a static library from one
mock definitions header (`defs`). A test executable uses it by listing its
`name` in `mocks`: the executable links the library, and its codegen runs after
the mock codegen. The Meson helper generates gentest mocks only; the gMock and
Trompeloeil backends need CMake. See [Mocks](../mock_generation.md) for
writing mock definitions and expectations.

```meson
gentest_textual_mocks = [
  {
    'name': 'app_mocks',
    'defs': 'tests/mock_defs.hpp',
    'public_header': 'app_mocks.hpp',
    'source_includes': ['tests'],
  },
]

gentest_textual_suites = [
  {
    'name': 'my_tests',
    'declaration_headers': ['tests/cases.hpp'],
    'source_includes': ['tests'],
    'mocks': ['app_mocks'],
  },
]

subdir('subprojects/gentest/meson/textual')
```

Write the mock definitions header and the test as in
[Mocks](../mock_generation.md). The test includes the generated header by its
`public_header` name, for example `#include "app_mocks.hpp"`.

| Field | Default | Meaning |
| --- | --- | --- |
| `name` | required | Static library name, and the name that `mocks` refers to. |
| `id` | `name` | Used in generated file names and in the codegen target name `gen_<id>`. |
| `kind` | `'textual'` | Any other value stops configuration. |
| `defs` | required | The mock definitions header, one file. |
| `public_header` | `<name>.hpp` | File name of the generated header that tests include. |
| `source_includes` | `[]` | Include directories for codegen and for the compiler. |
| `clang_args` | `[]` | Extra arguments for codegen only. |
| `cpp_args` | `[]` | Extra arguments for the compiler only. |
| `include_directories` | `[]` | `include_directories()` objects for the compiler. |
| `dependencies` | `[]` | Meson dependencies for the library. |
| `install` | `false` | Installs the static library. |
| `build_by_default` | `true` | Applies to the codegen step only. |

Codegen scans the `defs` header with `gentest/mock.h` included first.

## Variables you can use

After `subdir()`, the helper leaves these dictionaries, keyed by `name`, in
your project's scope:

| Variable | Value for each entry |
| --- | --- |
| `gentest_textual_suite_targets` | The test executable. |
| `gentest_textual_suite_codegen_targets` | The codegen custom target that writes the registration sources and the artifact manifest. |
| `gentest_textual_mock_targets` | The mock static library. |
| `gentest_textual_mock_codegen_targets` | The mock codegen custom target. |
| `gentest_textual_mock_codegen_dependencies` | A dependency that orders compilation after the mock codegen. Add it to your own targets that include a generated mock header. |
| `gentest_textual_mock_public_headers` | The generated public header. |

The gentest subproject also defines variables that you can read with
`gentest_sp.get_variable('<name>')`:

| Variable | Contents |
| --- | --- |
| `gentest_dep` | The runtime plus gentest's `main()`. `dependency('gentest')` returns it once the subproject is configured. |
| `gentest_runtime_dep` | The runtime library. |
| `gentest_main_dep` | gentest's `main()` (`gentest_main`); use it together with `gentest_runtime_dep`. |
| `gentest_codegen` | The resolved path of `gentest_codegen`. |
| `gentest_codegen_tool_args` | Extra codegen arguments: `--host-clang <path>` when `codegen_host_clang` is set. |
| `gentest_public_include`, `gentest_public_include_path` | gentest's `include` directory, as an `include_directories()` object and as an absolute path. |
| `gentest_third_party_include`, `gentest_third_party_include_path` | gentest's bundled `third_party/include` directory, in the same two forms. |

## Providing the subproject

A full gentest checkout works as the subproject, and so does a Meson wrap that
places gentest in `subprojects/gentest`. A trimmed copy needs `meson.build`,
`meson_options.txt`, `meson/`, `include/`, `src/` and `third_party/include/`.
It also needs a `tests/` directory, which may be empty: gentest's `meson.build`
names it as an include directory even when `build_self_tests` is `false`.

## Generated files and rebuilds

The codegen target `gen_<id>` writes the registration sources
(`*.header_registration.gentest.cpp`), a depfile and the artifact manifest
`<name>.artifact_manifest.json` (see
[Codegen artifact protocols](../codegen_artifact_protocols.md)). The
registration sources never include your `source`; the linker resolves your
definitions.

Codegen writes depfiles, so Meson and Ninja rerun it when a header it read
changes. The helper also passes the header files (`.h`, `.hh`, `.hpp`, `.hxx`,
`.inc`) in the directories of `defs`, `source`, `declaration_headers` and
`source_includes` to codegen as inputs. That list is fixed at `meson setup`:
run `meson setup --reconfigure` after you add header files.

## Limitations and tested example

A `kind` other than `'textual'` stops configuration with
`gentest Meson helper is textual-only; named modules are intentionally unsupported`.
Meson on Windows is untested: the consumer test skips there.

[`tests/downstream/meson_wrap_consumer`](../../tests/downstream/meson_wrap_consumer)
is a complete consumer project with a mock target, an out-of-line test
executable and a header-only one. The CTest test `gentest_meson_wrap_consumer`
builds and runs it, and CI runs it on Ubuntu 24.04 and Fedora 43. To run that
test in a gentest build tree:

```bash
ctest --preset=debug-system --output-on-failure -R '^gentest_meson_wrap_consumer$'
```
