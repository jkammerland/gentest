# Contributing to gentest

This file collects contributor-focused workflows that do not need to live on the main README landing page.

## Build & test

Preferred local flow:

```bash
cmake --preset=debug-system
cmake --build --preset=debug-system
ctest --preset=debug-system --output-on-failure
```

## Formatting

Format edited C/C++ files explicitly:

```bash
clang-format -i path/to/file.cpp path/to/file.hpp
```

Run the CI-aligned format gate:

```bash
scripts/check_clang_format.sh
```

## Clang-tidy

Run the same `clang-tidy` script CI uses after configuring a Clang-based `debug-system` build:

```bash
cmake --preset=debug-system -DGENTEST_ENABLE_PACKAGE_TESTS=OFF \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
scripts/check_clang_tidy.sh build/debug-system
```

If CMake cannot discover the matching LLVM/Clang packages automatically, also pass `-DLLVM_DIR=/path/to/lib/cmake/llvm -DClang_DIR=/path/to/lib/cmake/clang`.

The script checks the translation units present in the active compile database, remapping gentest `tu_*.gentest.*` shims back to their original repo sources when possible, so the CI-aligned `debug-system` lane covers the configured `src/`, `tools/`, `tests/`, and public module units. It also surfaces diagnostics from matching repo headers included by those translation units, while still excluding generated fixture outputs outside the active preset.

When the active compile database references generated explicit mock/codegen surfaces, `scripts/check_clang_tidy.sh` materializes any generated mock/codegen targets referenced by the active compile database before running clang-tidy, so a configure-only build dir is enough for the CI-aligned lint flow.

For the vcpkg-backed static-analysis workflow, the `tidy` / `tidy-fix` presets still exist:

```bash
cmake --preset=tidy
cmake --build --preset=tidy
ctest --preset=tidy --output-on-failure

cmake --preset=tidy-fix
cmake --build --preset=tidy-fix
```

## Coverage

The dedicated GitHub `coverage` workflow runs on Fedora 43 with system Clang and the `coverage-system` preset. The local CI-aligned flow is:

```bash
python3 -m pip install --upgrade gcovr==8.6
# On distro-managed Python, add --break-system-packages or use a virtualenv.
cmake --preset=coverage-system -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build --preset=coverage-system
find build/coverage-system -name '*.gcda' -delete
rm -rf build/coverage-system/coverage-report
ctest --preset=coverage-system --output-on-failure --parallel 1
python3 scripts/coverage_hygiene.py \
  --build-dir build/coverage-system \
  --ignore-statuses stamp_mismatch \
  --gcov llvm-cov gcov
python3 scripts/coverage_report.py --build-dir build/coverage-system
```

If CMake cannot discover the matching LLVM/Clang packages automatically, also pass `-DLLVM_DIR=/path/to/lib/cmake/llvm -DClang_DIR=/path/to/lib/cmake/clang`.

The coverage report reads policy from `scripts/coverage_hygiene.toml`, scopes totals to repo-owned files in the implementation trees under `src/` and `tools/src/`, includes internal headers in those trees, excludes intentional exemptions listed there, writes a Markdown summary to `build/coverage-system/coverage-report/summary.md`, and writes a per-file HTML report under `build/coverage-system/coverage-report/`.

## Before opening a PR

Run the relevant local checks for your change. At minimum, that usually means:

```bash
ctest --preset=debug-system --output-on-failure
scripts/check_clang_format.sh
```

If you touched lint scripts, workflow contracts, or coverage tooling, also run the matching contract tests under `ctest --preset=debug-system`.
