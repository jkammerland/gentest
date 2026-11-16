# Contributing to gentest

This page is for people who change gentest itself: building and testing this
repository, formatting, linting, coverage, CI and pull requests. To use gentest
in your own project, start with the [README](README.md).

## Build and test

The `debug-system` presets build the repository against the LLVM/Clang
installed on your machine and run every test:

```bash
cmake --preset=debug-system
cmake --build --preset=debug-system
ctest --preset=debug-system --output-on-failure
```

You need CMake 3.31 or newer, Ninja, a C++20 compiler and matching LLVM/Clang
development packages. The install guides for [Linux](docs/install/linux.md) and
[macOS](docs/install/macos.md) list the packages; [Windows](#windows) and
[macOS](#macos) builds of this repository have their own sections below. The
repository's own toolchain is LLVM 23, see
[Repository LLVM toolchain](#repository-llvm-toolchain).

While you work:

- Add `-DGENTEST_ENABLE_PACKAGE_TESTS=OFF` to the configure command for faster
  local runs. The `debug-system` and `release-system` presets turn the
  installed-package tests on.
- Run part of the suite with `ctest --preset=debug-system -R <regex>`, or by
  label with `-L <label>`.
- Test executables are built under `build/debug-system/tests/`, for example
  `gentest_unit_tests`; run one with `--list` to see its cases.
- Rebuilding a test target reruns codegen when its inputs changed:
  `cmake --build --preset=debug-system --target gentest_unit_tests`.

Other presets in `CMakePresets.json`:

| Preset | Use |
| --- | --- |
| `release-system` | Release build against the system LLVM/Clang |
| `debug-system-cxx23` | Builds the test executables as C++23 |
| `debug-system-ccache`, `release-system-ccache` | Use `ccache` as the compiler launcher of the main build |
| `alusan-system` | AddressSanitizer, LeakSanitizer and UndefinedBehaviorSanitizer |
| `tsan-system` | ThreadSanitizer (Linux only) |
| `coverage-system` | Coverage instrumentation, see [Coverage](#coverage) |
| `host-codegen` | Builds only `gentest_codegen`, for cross builds |
| `aarch64-qemu`, `riscv64-qemu` | Cross builds tested under QEMU, see [Cross-testing gentest under QEMU](docs/cross_compile/linux_qemu.md) |
| `debug`, `release`, `tidy`, `tidy-fix`, `alusan`, `release-package` | Take dependencies from vcpkg: set `VCPKG_ROOT` to a vcpkg checkout first, or configure fails |

Each sanitizer preset also has a workflow preset that configures, builds and
tests in one step:

```bash
cmake --workflow --preset=alusan-system
cmake --workflow --preset=tsan-system
```

### Repository LLVM toolchain

The repository pins LLVM 23 in `scripts/llvm_toolchain.json`. Formatting,
clang-tidy and the primary CI jobs use it, and the lint scripts and `tidy`
presets reject any other LLVM major. The build itself accepts LLVM 19 or newer,
and CI runs compatibility jobs with LLVM 19 to 22.

`scripts/llvm_tools.py` finds the LLVM 23 tools:

```bash
python3 scripts/llvm_tools.py resolve clang++
python3 scripts/llvm_tools.py check-toolchain
```

`resolve <tool>` prints the path of an LLVM 23 tool (`clang`, `clang++`,
`clang-format`, `clang-tidy`, `clang-scan-deps`, `llvm-cov` or `llvm-config`),
taking the first of these that exists:

1. the tool's override variable: `GENTEST_CLANG`, `GENTEST_CLANGXX`,
   `CLANG_FORMAT_BIN`, `CLANG_TIDY_BIN`, `GENTEST_CODEGEN_CLANG_SCAN_DEPS`,
   `GENTEST_LLVM_COV` or `GENTEST_LLVM_CONFIG`;
2. `$GENTEST_LLVM_PREFIX/bin/<tool>`;
3. `<tool>-23`, then `<tool>`, on `PATH`.

`check-toolchain` also checks the LLVM and Clang CMake packages.

`scripts/setup_llvm.sh` (root or sudo) installs LLVM 23 the way CI does: the
apt.llvm.org packages on Ubuntu, or the upstream archive under
`/opt/llvm-23.1.0` on Fedora x86_64.

CMake looks for LLVM through the first `llvm-config-<N>` (23 down to 19) or
`llvm-config` on `PATH`. If it finds the wrong installation, pass
`-DLLVM_DIR=<prefix>/lib/cmake/llvm -DClang_DIR=<prefix>/lib/cmake/clang`.
`"$(python3 scripts/llvm_tools.py resolve llvm-config)" --cmakedir` prints the
`LLVM_DIR` of your LLVM 23 installation.

LLVM's CMake package also needs the development files of the libraries LLVM
was built with, such as zlib, zstd and libxml2. If configure reports a missing
target such as `ZLIB::ZLIB`, install them or add their prefix to
`CMAKE_PREFIX_PATH`.

### Faster rebuilds with ccache

On Unix hosts with a GNU-style Clang or GCC driver, `GENTEST_TEST_COMPILER_CACHE`
puts `ccache` in front of the main build and of the nested builds that tests
configure. CI uses the same settings:

```bash
export GENTEST_TEST_COMPILER_CACHE=ON
export CCACHE_DIR="$PWD/build/compiler-cache"
export CCACHE_COMPILERCHECK=content
cmake --preset=debug-system
cmake --build --preset=debug-system
ctest --preset=debug-system --output-on-failure
```

- The option defaults to the `GENTEST_TEST_COMPILER_CACHE` environment
  variable, otherwise `OFF`; `-D` also sets it.
- Configure fails if `CMAKE_<LANG>_COMPILER_LAUNCHER` names a launcher other
  than ccache. Turn the option off to use your own launcher.
- Compilations ccache can't cache, including named modules, run uncached.

### Windows

Validate Windows changes with `RelWithDebInfo` builds: CI builds Windows only in
that configuration, and some death tests hang in Windows Debug builds. Pass
`--config RelWithDebInfo` to the build and `-C RelWithDebInfo` to CTest.

Start from a Developer PowerShell for Visual Studio (x64), so that `cl` and the
Windows SDK are found. From an ordinary PowerShell window, load it with
`Launch-VsDevShell.ps1`, then return to your checkout:

```powershell
$checkout = Get-Location
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64
Set-Location $checkout
```

Point the session at an LLVM release package (this example uses
`C:\Tools\llvm-23.1.0`):

```powershell
$llvm = 'C:\Tools\llvm-23.1.0'
$env:LLVM_BIN = "$llvm\bin"
$env:PATH = "$env:LLVM_BIN;$env:PATH"
$env:LLVM_DIR = "$llvm\lib\cmake\llvm"
$env:Clang_DIR = "$llvm\lib\cmake\clang"
```

Build and test with Clang:

```powershell
cmake --preset=debug-system -B build/relwithdebinfo-system `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_C_COMPILER="$env:LLVM_BIN\clang.exe" `
  -DCMAKE_CXX_COMPILER="$env:LLVM_BIN\clang++.exe" `
  -DLLVM_DIR="$env:LLVM_DIR" `
  -DClang_DIR="$env:Clang_DIR"
cmake --build build/relwithdebinfo-system --config RelWithDebInfo
ctest --test-dir build/relwithdebinfo-system -C RelWithDebInfo --output-on-failure
```

Or with MSVC, using the same LLVM for `gentest_codegen`:

```powershell
$msvcBuildDir = 'build\relwithdebinfo-system-msvc'
cmake -S . -B $msvcBuildDir -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl `
  -Dgentest_BUILD_TESTING=ON -DGENTEST_ENABLE_PACKAGE_TESTS=ON `
  -DLLVM_DIR="$env:LLVM_DIR" -DClang_DIR="$env:Clang_DIR"
cmake --build $msvcBuildDir --config RelWithDebInfo
ctest --test-dir $msvcBuildDir -C RelWithDebInfo --output-on-failure
```

Notes:

- With the LLVM 23.1.0 package, CI also installs `zlib`, `zstd` and `libxml2`
  for `x64-windows` with vcpkg and passes that install prefix as
  `CMAKE_PREFIX_PATH` (`.github/workflows/cmake.yml`).
- With Clang, a top-level gentest build selects the static release C runtime
  (unless you set `CMAKE_MSVC_RUNTIME_LIBRARY`), turns off iterator debugging
  to match prebuilt LLVM packages, and sets `FMT_USE_CONSTEVAL=0`
  (`cmake/GentestDependencies.cmake`).
- Some LLVM Windows packages refer to a `diaguids.lib` path that doesn't exist
  on your machine. The build replaces it with the newest Visual Studio DIA SDK
  copy it finds (`gentest_patch_llvm_missing_diaguids()` in
  `cmake/GentestLlvmDependencyShims.cmake`), so install the DIA SDK if the link
  step mentions `diaguids.lib`.
- If you must test a Debug configuration, `-DGENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS=ON`
  disables the death tests that hang there.
- [Windows troubleshooting](docs/windows_troubleshooting.md) covers LLVM
  discovery and codegen problems.

### macOS

Homebrew installs `llvm@23` keg-only, so CMake doesn't find it on its own. Pass
its package directories, and set `GENTEST_LLVM_PREFIX` so that
`scripts/llvm_tools.py` finds its tools:

```bash
llvm_prefix="$(brew --prefix llvm@23)"
export GENTEST_LLVM_PREFIX="$llvm_prefix"
cmake --preset=debug-system -DLLVM_DIR="$llvm_prefix/lib/cmake/llvm" -DClang_DIR="$llvm_prefix/lib/cmake/clang"
```

To build the public modules too, add `-DGENTEST_ENABLE_PUBLIC_MODULES=ON` and
pass Homebrew's `clang` and `clang++` as `CMAKE_C_COMPILER` and
`CMAKE_CXX_COMPILER`. CMake can't scan C++ module dependencies with AppleClang;
[Named modules](docs/modules.md#requirements) lists the requirements.

## Formatting

C and C++ sources follow `.clang-format` (LLVM-based, 4-space indent,
140-column limit), applied with LLVM 23's clang-format. Format the files you
changed:

```bash
"$(python3 scripts/llvm_tools.py resolve clang-format)" -i path/to/file.cpp path/to/file.hpp
```

Then run the check CI runs, a dry run over the tracked C and C++ files under
`include/`, `src/`, `tests/`, `tools/`, `examples/async/` and
`experimental/harness/`:

```bash
scripts/check_clang_format.sh
```

## Static analysis

CI's lint job runs `scripts/check_clang_tidy.sh` on a configured `debug-system`
build tree. Run the same check:

```bash
cmake --preset=debug-system -DGENTEST_ENABLE_PACKAGE_TESTS=OFF \
  -DCMAKE_C_COMPILER="$(python3 scripts/llvm_tools.py resolve clang)" \
  -DCMAKE_CXX_COMPILER="$(python3 scripts/llvm_tools.py resolve clang++)"
scripts/check_clang_tidy.sh build/debug-system
```

If CMake doesn't find the LLVM 23 packages, also pass `-DLLVM_DIR` and
`-DClang_DIR` (see [Repository LLVM toolchain](#repository-llvm-toolchain)).

A configure-only build tree is enough: the script first builds the generated
targets that the compile database refers to, then runs clang-tidy with warnings
as errors on every repository translation unit in the database and the
repository headers they include. Generated sources and `third_party/` are not
checked.

- `scripts/check_clang_tidy.sh build/debug-system fix` applies the suggested
  fixes instead, one file at a time.
- `CLANG_TIDY_JOBS=<N>` sets how many files are checked in parallel (default:
  the number of CPUs; CI uses 4).
- `CLANG_TIDY_BIN` selects the clang-tidy executable; it must be LLVM 23.

The `tidy` and `tidy-fix` presets run clang-tidy during a vcpkg-based build
instead (set `VCPKG_ROOT` first):

```bash
cmake --preset=tidy
cmake --build --preset=tidy
ctest --preset=tidy --output-on-failure
```

`cmake --preset=tidy-fix` followed by `cmake --build --preset=tidy-fix` applies
the fixes during the build.

## Coverage

The coverage gate measures line and branch coverage of gentest's
implementation files under `src/` and `tools/src/`, including internal headers
in those trees, and fails below 75% of lines or 45% of branches. CI runs it on
Fedora 43 with LLVM 23. To run it locally on Linux:

```bash
python3 -m pip install --upgrade gcovr==8.6
cmake --preset=coverage-system \
  -DCMAKE_C_COMPILER="$(python3 scripts/llvm_tools.py resolve clang)" \
  -DCMAKE_CXX_COMPILER="$(python3 scripts/llvm_tools.py resolve clang++)"
cmake --build --preset=coverage-system
find build/coverage-system -name '*.gcda' -delete
rm -rf build/coverage-system/coverage-report
ctest --preset=coverage-system --output-on-failure --parallel 1
llvm_cov="$(python3 scripts/llvm_tools.py resolve llvm-cov)"
python3 scripts/coverage_hygiene.py --build-dir build/coverage-system --ignore-statuses stamp_mismatch --gcov "$llvm_cov" gcov
python3 scripts/coverage_report.py --build-dir build/coverage-system --gcov "$llvm_cov" gcov
```

- On a distribution-managed Python, add `--break-system-packages` to the
  `pip install` command, or use a virtual environment.
- Delete old `.gcda` files and the old report before each run, and run CTest
  serially: parallel test processes race on shared `.gcda` files.
- Pass the LLVM 23 `llvm-cov` to both scripts; without `--gcov` they use the
  `llvm-cov` on `PATH`, which may be another version.
- The report lands in `build/coverage-system/coverage-report/` (`summary.md`
  and `index.html`).

[Coverage hygiene and reporting](docs/coverage_hygiene.md) explains the
statuses, the policy file and how CI runs the gate.

## Interactive async demos

`gentest_async_live_slow_tests` holds slow async cases that show live terminal
progress. Build it and run its ping-pong demo in a terminal:

```bash
cmake --build --preset=debug-system --target gentest_async_live_slow_tests
build/debug-system/tests/gentest_async_live_slow_tests --filter='async_live_slow/pingpong/*'
```

The demo pauses 600 ms at each handoff so you can watch it.
`GENTEST_ASYNC_DEMO_FAST=1` shortens the pauses to 50 ms; the CTest checks of
these demos use that mode.

## Add or change tests

- Test directories live under `tests/<name>/`. The main suites (`unit`,
  `integration`, `fixtures`, `mocking` and others) are built by
  `gentest_add_suite(<name>)` in `tests/CMakeLists.txt` (defined in
  `cmake/GentestTests.cmake`), which compiles `tests/<name>/cases.cpp` into
  `gentest_<name>_tests`, links `gentest_main` and attaches codegen. Other test
  executables are defined directly and call `gentest_attach_codegen(<target>)`.
- Declare annotated cases in a header that `cases.cpp` includes; definitions
  can stay out of line in `cases.cpp`. [Writing tests](docs/writing_tests.md)
  explains the rules.
- When you add, remove or rename cases, update every expectation that counts
  or lists the cases of that executable, in `tests/CMakeLists.txt` and
  `tests/cmake/Regressions.cmake`:
  - `gentest_add_check_counts(...)`: the `PASS`, `FAIL` and `SKIP` numbers
    (and `BLOCKED`, `XFAIL`, `XPASS` where given), or `CASES` with `LIST`;
  - `gentest_add_check_inventory(...)`: `CASES`, or the
    `tests/expected/<suite>.list` file named by `EXPECTED_LIST_FILE`, which
    has one `[PASS] <case>` (or `[FAIL]`, `[SKIP]`, `[BLOCKED]`, `[XFAIL]`,
    `[XPASS]`) line per case;
  - count tables that loops pass to these functions.
- For a check that runs a program and inspects its exit code or output, use
  `gentest_add_cmake_script_test(NAME ... PROG ... SCRIPT ...)` with a script
  from `tests/cmake/scripts/`, and pass the variables the script reads in
  `DEFINES` (for example `TIMEOUT_SEC` and `EXPECT_RC` for
  `CheckNoTimeout.cmake`).
- The Glaze and cbor_tags recording adapters have their own tests, off by
  default. Install the opt-in vcpkg features `record-glaze` and `record-cbor`
  (the repository's `vcpkg-configuration.json` adds the pinned overlay ports in
  `vcpkg-overlays/`), then configure with
  `-DGENTEST_ENABLE_RECORDING_ADAPTER_TESTS=ON` and the vcpkg install prefix on
  `CMAKE_PREFIX_PATH`. Use Clang: with Glaze, the tested GCC 16's intrinsic
  headers break the codegen scan (see [examples/recording](examples/recording/README.md)).

  ```bash
  vcpkg install --x-feature=record-glaze --x-feature=record-cbor
  cmake --preset=debug-system -DGENTEST_ENABLE_RECORDING_ADAPTER_TESTS=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_PREFIX_PATH=$PWD/vcpkg_installed/x64-linux
  ```

  Configure fails if Glaze 8.3.0 or cbor_tags 0.24.0 is missing.
  `python3 scripts/validation_suites.py recording --work <dir> --serializers <prefix>`
  runs the same build and tests as the `runtime-recording` workflow
  (`.github/workflows/recording.yml`).

## CI

- The lint workflow runs on every pull request and every push to `master`:
  `scripts/check_clang_format.sh` and `scripts/check_clang_tidy.sh` with LLVM 23.
- Full CI covers the Linux, Windows and macOS build matrix, sanitizers,
  coverage, packaging, the QEMU cross builds, Meson, Xmake and Bazel, and more.
  It runs only when a maintainer requests it (a checkbox in the bot comment, or
  a `/full-ci` comment), and its results are informational; see
  [Continuous integration](docs/ci_profiles.md).
- [Private local validation](docs/local_validation.md) runs the same kinds of
  checks on your own Linux, Windows and macOS machines and keeps the results
  private.

## Commits and pull requests

- Write commit messages as Conventional Commits, `type(scope): subject`, with
  a short imperative subject, for example
  `feat(codegen): attach generated registration shims`. Mark breaking changes
  with `!`, as in `feat(allure)!: ...`. Explain the reason in the body when
  needed, and add trailers such as `Refs: #123`.
- In the pull request, describe the scope, list the presets you ran (for
  example `debug-system`, `alusan-system`, `tsan-system`), and include the
  failing output for regressions or flaky tests.
- Before you open it, run at least:

  ```bash
  ctest --preset=debug-system --output-on-failure
  scripts/check_clang_format.sh
  ```

- If you changed scripts, workflows or docs, also run
  `ctest --preset=debug-system -R contract` and
  `ctest --preset=debug-system -L scripts`.

## More for contributors

| Page | What it covers |
| --- | --- |
| [Continuous integration](docs/ci_profiles.md) | Lint, requesting full CI, what full CI runs |
| [Private local validation](docs/local_validation.md) | Running the full validation on your own machines |
| [Coverage hygiene and reporting](docs/coverage_hygiene.md) | The coverage gate, its statuses and policy file |
| [Measure adoption and edit/build/test costs](docs/developer_build_measurements.md) | `scripts/bench_developer_cycle.py` and the other build-time measurement scripts |
| [Cross-testing gentest under QEMU](docs/cross_compile/linux_qemu.md) | Building gentest for aarch64 and riscv64 and testing it under QEMU |
| [Packaging](docs/packaging.md) | Release archives and signatures, and the maintainer steps for cutting a release |
| [Codegen artifact protocols](docs/codegen_artifact_protocols.md) | What `gentest_codegen` writes, for build-system integrations |
| [Libclang deferred diagnostic storage leak](docs/issues/libclang_dependent_diagnostic_leak.md), [Clang constraint-deduction failure ownership leak](docs/issues/libclang_deduction_failure_leak.md) | Upstream Clang issues that CI tracks |
| [Design notes](docs/stories/README.md) | Why past changes were made, with each note's status |
| [AGENTS.md](AGENTS.md) | Instructions for coding agents working in this repository |

The [documentation index](docs/index.md) lists every page.
