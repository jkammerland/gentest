# Choosing a build system

gentest works with CMake, Meson, Xmake and Bazel. This page compares what each
integration supports and how each one spells the same settings, so you can pick
one and go to its guide.

CMake is the primary integration: it supports every feature, and the
[README quick start](../../README.md) and the install guides for
[Linux](../install/linux.md), [macOS](../install/macos.md) and
[Windows](../install/windows.md) use it.

## What each build system supports

| | CMake | Meson | Xmake | Bazel |
| --- | --- | --- | --- | --- |
| Guide | [README](../../README.md) and the install guides | [Meson](meson.md) | [Xmake](xmake.md) | [Bazel](bazel.md) |
| How you add gentest | Install it and call `find_package(gentest CONFIG REQUIRED)`, or add the source tree with `add_subdirectory()` | Put the gentest source tree in `subprojects/gentest`, directly or through a wrap | Install it and load `share/gentest/xmake/gentest.lua` from the install | `bazel_dep(name = "gentest")` with an override that points at the gentest source, such as `local_path_override` |
| Header registration | Yes | Yes | Yes | Yes |
| Module registration | Yes (`MODULE_REGISTRATION`) | No | Yes, with a Clang target toolchain | Yes, with Clang and `--experimental_cpp_modules` |
| Attach codegen with | `gentest_attach_codegen()` | An entry in `gentest_textual_suites` | `gentest_attach_codegen({...})` | `gentest_attach_codegen_textual()`, `gentest_attach_codegen_modules()` |
| Add a mock target with | `gentest_add_mocks()` | An entry in `gentest_textual_mocks` | `gentest_add_mocks({...})` | `gentest_add_mocks_textual()`, `gentest_add_mocks_modules()` |
| Mock backends | gentest, gMock, Trompeloeil | gentest | gentest | gentest |
| Mock definitions in a named module | Yes | No | Yes | Yes |
| Run the tests with | `gentest_discover_tests()` and `ctest` | `meson test` | `xmake run <target>`, `xmake test` | `bazel test` |
| Register death tests with | `gentest_discover_tests()` | The suite's `death_tests` list | `gentest_add_death_test({...})` | `gentest_death_test()` |
| Tested in CI on | Linux, macOS, Windows | Linux | Linux | Linux (macOS: codegen action-cache test only) |

[Header registration](../writing_tests.md) (the default) reads annotated
declarations from headers; [module registration](../modules.md) puts cases in
named-module interface units. The Meson, Xmake and Bazel APIs call header
registration "textual", as in `gentest_textual_suites`, `kind = "textual"` and
`gentest_attach_codegen_textual()`. The gMock and Trompeloeil backends are
CMake-only; [Mocks](../mock_generation.md) describes them.

A few more limits:

- A Meson mock entry and Bazel's `gentest_add_mocks_textual()` take exactly one
  mock definitions header per mock target.
- Bazel on Windows needs a packaged exec toolchain; the local development
  fallback doesn't run there (see [Bazel](bazel.md#exec-toolchain-contract)).
- Named modules in CMake have their own
  [compiler and generator requirements](../modules.md#requirements).

## What every build needs

On the machine that runs the build, every integration needs:

- `gentest_codegen`, built for that machine against LLVM/Clang 19 or newer,
  from the same gentest version as the runtime you link;
- a Clang executable on that machine, the host Clang, which codegen runs for
  Clang's built-in headers and for named modules. It can differ from the
  compiler that builds your tests;
- `clang-scan-deps`, when you use named modules.

Codegen also needs the target flags, such as `--target` and `--sysroot`, that
your tests are compiled with. CMake passes them to codegen for you. With Meson,
Xmake and Bazel you repeat them as extra Clang arguments.
[Host toolchains and cross-compiling](host_toolchain_sysroots.md) explains how
codegen finds the host Clang and how to build for another target.

## Settings by build system

| Setting | CMake | Meson | Xmake | Bazel |
| --- | --- | --- | --- | --- |
| `gentest_codegen` | Found in the installed package. Set `GENTEST_CODEGEN_EXECUTABLE` or `GENTEST_CODEGEN_TARGET` when you cross-compile or when your gentest has no code generator. | `codegen_path` option; a relative path is resolved against the gentest subproject directory. Default: `build/host-codegen/tools/gentest_codegen` there. | `codegen.exe` in `gentest_configure()`, or the `GENTEST_CODEGEN` environment variable. Set one with an installed gentest; without them the helper tries to build `gentest_codegen` with CMake from a gentest checkout. | The `codegen` label of the registered `gentest_codegen_toolchain` |
| Host Clang | `GENTEST_CODEGEN_HOST_CLANG` (optional) | `codegen_host_clang` option (optional) | `codegen.clang`, or the `GENTEST_CODEGEN_HOST_CLANG` environment variable (optional) | The `clang` label of the toolchain (required) |
| `clang-scan-deps` | `GENTEST_CODEGEN_CLANG_SCAN_DEPS` (optional) | Not used | `codegen.scan_deps`, or the `GENTEST_CODEGEN_CLANG_SCAN_DEPS` environment variable (optional) | The `clang_scan_deps` label of the toolchain |
| Extra Clang arguments for codegen | `CLANG_ARGS` of `gentest_attach_codegen()` or `gentest_add_mocks()`; `GENTEST_CODEGEN_DEFAULT_CLANG_ARGS` for every target | `clang_args` of an entry; codegen only | `clang_args` of a call, or `gentest_common_cxxflags` in `gentest_configure()`; both also go to the compiler | `clang_args`; also goes to the compiler |
| Target compiler and sysroot | A toolchain file; gentest forwards the target and sysroot to codegen | A native or cross file; repeat the target flags in `clang_args` | `xmake f` options; repeat the target flags in `gentest_common_cxxflags` | Your C++ toolchain and platforms; repeat the target flags in `clang_args` |
| Generated files | `OUTPUT_DIR` of `gentest_attach_codegen()`, a concrete path without generator expressions. Default: `gentest/<target id>` in the current binary directory. | The helper's build directory, `subprojects/gentest/meson/textual` in your build tree | `output_dir` of each call (required) | `gen/<name>` in the package's output directory |

"Optional" means codegen finds the tool itself when you don't set it.
[Host toolchains and cross-compiling](host_toolchain_sysroots.md) lists the
lookup order for each tool and explains when to set them.

## CMake

The installed package provides the `gentest::gentest`, `gentest::gentest_main`
and `gentest::gentest_runtime` targets and the CMake functions
`gentest_attach_codegen()`, `gentest_add_mocks()`, `gentest_link_mocks()` and
`gentest_discover_tests()`. A native build finds `gentest_codegen` in the same
install.

The package also needs the fmt CMake package of exactly the version gentest was
built with. The simplest setup installs gentest and fmt into the same prefix
and points `CMAKE_PREFIX_PATH` at it; a gentest build that fetched fmt itself
installs it there. Otherwise, pass `fmt_DIR` as well.

You can also add the gentest source tree with `add_subdirectory()`. To generate
registrations, either set `GENTEST_BUILD_CODEGEN=ON`,
which needs the LLVM/Clang development packages, or point
`GENTEST_CODEGEN_EXECUTABLE` at a built `gentest_codegen`.

Gentest's CMake helpers must come from one source tree or installation per
configure. Loading helpers from another copy is an error, including during a
`QUIET` package lookup.
`add_subdirectory()` does not redirect later `find_package(gentest)` calls.
When nested dependencies request Gentest as a package, the parent must arrange
for those requests to reuse its source copy, for example with FetchContent's
`OVERRIDE_FIND_PACKAGE`. Parent-provided package redirects, version checks and
component requirements retain their normal CMake behavior.

## Meson

You add the gentest source tree as a subproject, describe test executables and
mock targets in the `gentest_textual_suites` and `gentest_textual_mocks` lists,
and load the helper with `subdir('subprojects/gentest/meson/textual')`. You
build `gentest_codegen` separately with CMake. Named modules aren't supported.

Read [Meson](meson.md). The tested example is
[`tests/downstream/meson_wrap_consumer`](../../tests/downstream/meson_wrap_consumer).

## Xmake

You install gentest with CMake, load
the helper `share/gentest/xmake/gentest.lua` from the install, call
`gentest_configure()`, and then call `gentest_add_mocks()` and
`gentest_attach_codegen()` in your targets. Named modules need a Clang target
toolchain and `clang-scan-deps`.

Read [Xmake](xmake.md). The tested example,
[`tests/downstream/xmake_xrepo_consumer`](../../tests/downstream/xmake_xrepo_consumer),
consumes an installed gentest through a project-local xrepo package.

## Bazel

You add gentest as a Bzlmod dependency and load the macros from
`@gentest//bazel:defs.bzl`. Codegen runs the tools of an exec-platform
`gentest_codegen_toolchain` that you register, separate from your target C++
toolchain. For local development on Linux and macOS,
`GENTEST_BAZEL_LOCAL_CLANG` selects a fallback toolchain that builds
`gentest_codegen` from source.

Read [Bazel](bazel.md), starting with its
[exec toolchain contract](bazel.md#exec-toolchain-contract). The tested example
is [`tests/downstream/bazel_bzlmod_consumer`](../../tests/downstream/bazel_bzlmod_consumer).
