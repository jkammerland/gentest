# Bazel

This Bazel integration is currently a repo-local convenience path for the
classic handwritten suites in this repository. It is not yet a general
downstream Bazel rule set.

## Current scope

- Supports the classic suites in `tests/<suite>/cases.cpp`.
- Uses the shared per-TU helper in [`scripts/gentest_buildsystem_codegen.py`](../../scripts/gentest_buildsystem_codegen.py).
- Bootstraps `gentest_codegen` through a local CMake genrule.
- Does not currently support named-module suites, explicit mock targets, or a
  reusable downstream Bazel `gentest_suite(...)` package outside this repo.

The currently wired suites are:

- `gentest_unit_bazel`
- `gentest_integration_bazel`
- `gentest_fixtures_bazel`
- `gentest_skiponly_bazel`

There is also a generator lint target:

- `codegen_check_invalid`

`codegen_check_invalid` is a Bash-based repo check. Treat it as a Unix-like
helper target, not part of the cross-platform quickstart.

## Build and run

The simplest cross-platform local path is:

```bash
bazel test \
  //:gentest_unit_bazel \
  //:gentest_integration_bazel \
  //:gentest_fixtures_bazel \
  //:gentest_skiponly_bazel
```

If you want to validate build and runtime separately on Linux/macOS:

```bash
bazel build \
  //:gentest_unit_bazel \
  //:gentest_integration_bazel \
  //:gentest_fixtures_bazel \
  //:gentest_skiponly_bazel

./bazel-bin/gentest_unit_bazel
./bazel-bin/gentest_integration_bazel
./bazel-bin/gentest_fixtures_bazel
./bazel-bin/gentest_skiponly_bazel
```

On Linux/macOS, you can also run the Bash-only invalid-codegen smoke check:

```bash
bazel test //:codegen_check_invalid
```

## Toolchain notes

The Bazel path shells out to CMake to build `gentest_codegen`. That means:

- `cmake` must be available on `PATH`
- `python3` on Linux/macOS, `python` on Windows, must be available for the
  shared codegen helper
- the host needs the LLVM/Clang development packages required to build
  `gentest_codegen`
- this bootstrap is intentionally non-hermetic for now

If you want Bazel to use a specific host compiler, pass it through the action
environment, for example:

```bash
bazel test \
  //:gentest_unit_bazel \
  --action_env=CC=clang-20 \
  --action_env=CXX=clang++-20 \
  --host_action_env=CC=clang-20 \
  --host_action_env=CXX=clang++-20
```

If `gentest_codegen` cannot discover the Clang resource directory in your Bazel
environment, set it explicitly in the Bazel action environment:

```bash
bazel test \
  //:gentest_unit_bazel \
  --action_env=GENTEST_CODEGEN_RESOURCE_DIR="$(clang++ -print-resource-dir)"
```

If you prefer exporting it once in your shell, also forward it into Bazel
actions:

```bash
export GENTEST_CODEGEN_RESOURCE_DIR="$(clang++ -print-resource-dir)"
bazel test //:gentest_unit_bazel --action_env=GENTEST_CODEGEN_RESOURCE_DIR
```

## What Bazel generates

Per suite, the Bazel genrule writes:

- `gen/<suite>/tu_0000_<suite>_cases.gentest.cpp`
- `gen/<suite>/tu_0000_<suite>_cases.gentest.h`

The generated wrapper is then compiled by the corresponding `cc_test`.

## Adding another classic suite in this repo

1. Add `tests/<suite>/cases.cpp`.
2. Add `gentest_suite("<suite>")` in [`BUILD.bazel`](../../BUILD.bazel).
3. Run:

```bash
bazel test //:gentest_<suite>_bazel
```

## Limitations

- This path is currently limited to the phase-1 classic-suite integration.
- It is intentionally limited to classic/header-style suites.
- The `gentest_codegen` bootstrap rule is local and non-hermetic.
- If you need modules, explicit mock targets, package export, or a reusable
  consumer-facing integration, use the CMake path for now. Follow-up parity work
  is tracked in [`docs/stories/015_non_cmake_full_parity.md`](../stories/015_non_cmake_full_parity.md).
