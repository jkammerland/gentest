# Bazel

This Bazel integration is currently a repo-local convenience path for this
repository. It is not yet a general downstream Bazel rule set.

## Current scope

- Supports the classic suites in `tests/<suite>/cases.cpp`.
- Supports one repo-local explicit textual mock slice:
  - defs file: `tests/consumer/header_mock_defs.hpp`
  - generated public header: `gen/consumer_textual_mocks/gentest_consumer_mocks.hpp`
  - consumer test source: `tests/buildsystems/consumer_textual_cases.cpp`
- Uses the shared per-TU helper in [`scripts/gentest_buildsystem_codegen.py`](../../scripts/gentest_buildsystem_codegen.py).
- Bootstraps `gentest_codegen` through a local CMake genrule.
- Does not currently support named-module suites, module mock defs, or a
  reusable downstream Bazel `add_mocks(...)` / `attach_codegen(...)` rule set yet.

The currently wired suites are:

- `gentest_unit_bazel`
- `gentest_integration_bazel`
- `gentest_fixtures_bazel`
- `gentest_skiponly_bazel`

The additional repo-local explicit textual mock target is:

- `gentest_consumer_textual_bazel`

There is also a generator lint target:

- `codegen_check_invalid`

`codegen_check_invalid` is a Bash-based repo check. Treat it as a Unix-like
helper target, not part of the cross-platform quickstart.

## Bazel baseline

This repo-local Bazel path is currently pinned to Bazel `9.0.0` through
[`/.bazelversion`](../../.bazelversion).

- prefer `bazelisk` or a `bazel` wrapper backed by Bazelisk so the pin is
  respected automatically
- dependency resolution currently comes from [`MODULE.bazel`](../../MODULE.bazel)
- [`WORKSPACE`](../../WORKSPACE) is present only as the legacy workspace root
  marker; it is not the active dependency definition path

## Build and run

On Linux, the simplest local path is:

```bash
bazelisk test \
  //:gentest_unit_bazel \
  //:gentest_integration_bazel \
  //:gentest_fixtures_bazel \
  //:gentest_skiponly_bazel
```

If you want to validate build and runtime separately on Linux/macOS:

```bash
bazelisk build \
  //:gentest_unit_bazel \
  //:gentest_integration_bazel \
  //:gentest_fixtures_bazel \
  //:gentest_skiponly_bazel

./bazel-bin/gentest_unit_bazel
./bazel-bin/gentest_integration_bazel
./bazel-bin/gentest_fixtures_bazel
./bazel-bin/gentest_skiponly_bazel
```

For the textual mock slice, prefer build + direct execution:

```bash
bazelisk build //:gentest_consumer_textual_bazel
./bazel-bin/gentest_consumer_textual_bazel
```

On macOS with Homebrew LLVM, force Bazel onto the LLVM Clang toolchain first:

```bash
export CC=/opt/homebrew/opt/llvm/bin/clang
export CXX=/opt/homebrew/opt/llvm/bin/clang++
bazelisk build //:gentest_consumer_textual_bazel
./bazel-bin/gentest_consumer_textual_bazel
```

Some local container environments fail `bazel test` before the binary runs
because Bazel's shell test wrapper cannot execute in the sandbox. The direct
binary path above avoids that issue while still validating the generated mock
surface and consumer test target.

On Linux/macOS, you can also run the Bash-only invalid-codegen smoke check:

```bash
bazelisk test //:codegen_check_invalid
```

## Toolchain notes

The Bazel path shells out to CMake to build `gentest_codegen`. That means:

- `cmake` must be available on `PATH`
- `python3` on Linux/macOS, `python` on Windows, must be available for the
  shared codegen helper
- the host needs the LLVM/Clang development packages required to build
  `gentest_codegen`
- this bootstrap is intentionally non-hermetic for now

On macOS, the Bazel path expects Homebrew `cmake`, `ninja`, and LLVM under
their standard prefixes:

- `/opt/homebrew/bin/cmake`
- `/opt/homebrew/bin/ninja`
- `/opt/homebrew/opt/llvm/...`

If you want Bazel to use a specific host compiler, pass it through the action
environment, for example:

```bash
bazelisk test \
  //:gentest_unit_bazel \
  --action_env=CC=clang-20 \
  --action_env=CXX=clang++-20 \
  --host_action_env=CC=clang-20 \
  --host_action_env=CXX=clang++-20
```

If `gentest_codegen` cannot discover the Clang resource directory in your Bazel
environment, set it explicitly in the Bazel action environment:

```bash
bazelisk test \
  //:gentest_unit_bazel \
  --action_env=GENTEST_CODEGEN_RESOURCE_DIR="$(clang++ -print-resource-dir)"
```

If you prefer exporting it once in your shell, also forward it into Bazel
actions:

```bash
export GENTEST_CODEGEN_RESOURCE_DIR="$(clang++ -print-resource-dir)"
bazelisk test //:gentest_unit_bazel --action_env=GENTEST_CODEGEN_RESOURCE_DIR
```

## What Bazel generates

Per suite, the Bazel genrule writes:

- `gen/<suite>/tu_0000_<suite>_cases.gentest.cpp`
- `gen/<suite>/tu_0000_<suite>_cases.gentest.h`

The generated wrapper is then compiled by the corresponding `cc_test`.

For the textual mock slice, Bazel also writes:

- `gen/consumer_textual_mocks/consumer_textual_mocks_defs.cpp`
- `gen/consumer_textual_mocks/consumer_textual_mocks_anchor.cpp`
- `gen/consumer_textual_mocks/tu_0000_consumer_textual_mocks_defs.gentest.h`
- `gen/consumer_textual_mocks/consumer_textual_mocks_mock_registry.hpp`
- `gen/consumer_textual_mocks/consumer_textual_mocks_mock_impl.hpp`
- `gen/consumer_textual_mocks/consumer_textual_mocks_mock_registry__domain_0000_header.hpp`
- `gen/consumer_textual_mocks/consumer_textual_mocks_mock_impl__domain_0000_header.hpp`
- `gen/consumer_textual_mocks/gentest_consumer_mocks.hpp`
- `gen/consumer_textual_mocks/def_0000_header_mock_defs.hpp`
- staged textual support headers under `gen/consumer_textual_mocks/deps/`

The consumer `cc_test` then compiles the wrapper from
`gen/consumer_textual/tu_0000_consumer_textual_cases.gentest.cpp` and links the
generated mock library.

## Adding another classic suite in this repo

1. Add `tests/<suite>/cases.cpp`.
2. Add `gentest_suite("<suite>")` in [`BUILD.bazel`](../../BUILD.bazel).
3. Run:

```bash
bazelisk test //:gentest_<suite>_bazel
```

## Limitations

- This path currently supports:
  - classic per-TU suites
  - one in-tree textual explicit-mock slice
- It is still intentionally limited to classic/header-style suites.
- The `gentest_codegen` bootstrap rule is local and non-hermetic.
- Windows Bazel validation is currently blocked by a host-level Bazel bootstrap
  failure before the gentest repo is analyzed. That still reproduces on the
  current `9.0.0` pin and on local probes of Bazel `8.4.2` and `7.7.0`. Use
  Meson/Xmake/CMake on Windows for now.
- If you need named modules, module mock defs, reusable/public Bazel
  `add_mocks(...)` / `attach_codegen(...)` rules, or package/export parity, use
  the CMake path for now. Follow-up parity work is tracked in
  [`docs/stories/015_non_cmake_full_parity.md`](../stories/015_non_cmake_full_parity.md).
