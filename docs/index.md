# gentest docs index

>[!NOTE]
> This is a lightweight map of the docs set. Keep it short; add detail in the linked pages.

## Getting started

- Quick start: [`README.md`](../README.md)
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

## Fuzzing

- FuzzTest + Centipede long read (integration notes): [`docs/fuzzing/fuzztest_centipede_long_read.md`](fuzzing/fuzztest_centipede_long_read.md)

## Reviews

- Fuzzing MCP review (2026-01-11): [`docs/reviews/fuzzing_mcp_review_2026-01-11.md`](reviews/fuzzing_mcp_review_2026-01-11.md)

## Work items

- Correctness fixes + regression tests: [`docs/stories/001_p0_correctness_and_regression_tests.md`](stories/001_p0_correctness_and_regression_tests.md)
- Runtime outcomes (`skip()` / `xfail()`): [`docs/stories/002_runtime_skip_and_xfail.md`](stories/002_runtime_skip_and_xfail.md)
- Per‑TU registration wrappers (CMake): [`docs/stories/003_per_tu_registration_wrappers.md`](stories/003_per_tu_registration_wrappers.md)
- Deprecate manifest + port non‑CMake builds: [`docs/stories/004_deprecate_manifest_mode_and_port_non_cmake_builds.md`](stories/004_deprecate_manifest_mode_and_port_non_cmake_builds.md)
- Parallel `gentest_codegen` jobs (TU mode): [`docs/stories/005_codegen_parallelism_jobs.md`](stories/005_codegen_parallelism_jobs.md)
- Fuzzing support (research: FuzzTest / Centipede): [`docs/stories/003_fuzzing_research.md`](stories/003_fuzzing_research.md)
- Fuzzing support (FuzzTest / Centipede integration): [`docs/stories/004_fuzzing_fuzztest_integration.md`](stories/004_fuzzing_fuzztest_integration.md)
- Fuzzing API surface (no engine leakage): [`docs/stories/005_fuzzing_public_api_nonleakage.md`](stories/005_fuzzing_public_api_nonleakage.md)
- Fuzzing codegen backend abstraction: [`docs/stories/006_fuzzing_codegen_backend_abstraction.md`](stories/006_fuzzing_codegen_backend_abstraction.md)
- Fuzzing backend v1 (FuzzTest): [`docs/stories/007_fuzzing_fuzztest_backend.md`](stories/007_fuzzing_fuzztest_backend.md)
