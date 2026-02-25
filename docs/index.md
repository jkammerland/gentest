# gentest docs index

>[!NOTE]
> This is a lightweight map of the docs set. Keep it short; add detail in the linked pages.

## Getting started

- Quick start: [`README.md`](../README.md)
- C++20 module usage (`import gentest`): [`README.md#c20-modules`](../README.md#c20-modules)
- Previous (full) README snapshot: [`docs/archive/README_2026-01-03.md`](archive/README_2026-01-03.md)

## Install / build templates

- Linux: [`docs/install/linux.md`](install/linux.md)
- macOS: [`docs/install/macos.md`](install/macos.md)
- Windows: [`docs/install/windows.md`](install/windows.md)

## Cross-compiling

- Linux → aarch64 (QEMU): [`docs/cross_compile/linux_aarch64_qemu.md`](cross_compile/linux_aarch64_qemu.md)

## Troubleshooting / notes

- Windows troubleshooting: [`docs/windows_troubleshooting.md`](windows_troubleshooting.md)
- LLVM 21 location notes: [`docs/llvm21-location-api-fix.md`](llvm21-location-api-fix.md)
- Compilation database include-path notes: [`INCLUDE_PATH_FIX.md`](../INCLUDE_PATH_FIX.md)
- Fixture allocation hook: [`docs/fixtures_allocation.md`](fixtures_allocation.md)
- Traceability standards map: [`docs/traceability_standards.md`](traceability_standards.md)

## Work items

- Correctness fixes + regression tests: [`docs/stories/001_p0_correctness_and_regression_tests.md`](stories/001_p0_correctness_and_regression_tests.md)
- Runtime outcomes (`skip()` / `xfail()`): [`docs/stories/002_runtime_skip_and_xfail.md`](stories/002_runtime_skip_and_xfail.md)
- Per‑TU registration wrappers (CMake): [`docs/stories/003_per_tu_registration_wrappers.md`](stories/003_per_tu_registration_wrappers.md)
- Deprecate manifest + port non‑CMake builds: [`docs/stories/004_deprecate_manifest_mode_and_port_non_cmake_builds.md`](stories/004_deprecate_manifest_mode_and_port_non_cmake_builds.md)
- Parallel `gentest_codegen` jobs (TU mode): [`docs/stories/005_codegen_parallelism_jobs.md`](stories/005_codegen_parallelism_jobs.md)
- Runtime regressions and CMake check hardening: [`docs/stories/006_runtime_regressions_and_cmake_checks.md`](stories/006_runtime_regressions_and_cmake_checks.md)
