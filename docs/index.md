# gentest docs index

>[!NOTE]
> This is a lightweight map of the docs set. Keep it short; add detail in the linked pages.

## Getting started

- Quick start: [`README.md`](../README.md)
- Changelog: [`CHANGELOG.md`](../CHANGELOG.md)
- Modules guide: [`docs/modules.md`](modules.md)
- Previous (full) README snapshot: [`docs/archive/README_2026-01-03.md`](archive/README_2026-01-03.md)

## Install / build templates

- Linux: [`docs/install/linux.md`](install/linux.md)
- macOS: [`docs/install/macos.md`](install/macos.md)
- Windows: [`docs/install/windows.md`](install/windows.md)

## Downstream Buildsystem Guides

- Consumer overview: [`docs/buildsystems/downstream_consumers.md`](buildsystems/downstream_consumers.md)
- Meson (official textual-only wrap/subproject support): [`docs/buildsystems/meson.md`](buildsystems/meson.md)
- Xmake / xrepo (official installed-helper support): [`docs/buildsystems/xmake.md`](buildsystems/xmake.md)
- Bazel / Bzlmod (official source-package support): [`docs/buildsystems/bazel.md`](buildsystems/bazel.md)

## Cross-compiling

- Linux → aarch64 (QEMU): [`docs/cross_compile/linux_aarch64_qemu.md`](cross_compile/linux_aarch64_qemu.md)
- Linux → riscv64 (QEMU): [`docs/cross_compile/linux_riscv64_qemu.md`](cross_compile/linux_riscv64_qemu.md)

## Troubleshooting / notes

- Windows troubleshooting: [`docs/windows_troubleshooting.md`](windows_troubleshooting.md)
- LLVM 21 location notes: [`docs/llvm21-location-api-fix.md`](llvm21-location-api-fix.md)
- Fixture allocation hook: [`docs/fixtures_allocation.md`](fixtures_allocation.md)
- Coverage hygiene gate: [`docs/coverage_hygiene.md`](coverage_hygiene.md)
- Traceability standards map: [`docs/traceability_standards.md`](traceability_standards.md)

## Work items

- Correctness fixes + regression tests: [`docs/stories/001_p0_correctness_and_regression_tests.md`](stories/001_p0_correctness_and_regression_tests.md)
- Runtime outcomes (`skip()` / `xfail()`): [`docs/stories/002_runtime_skip_and_xfail.md`](stories/002_runtime_skip_and_xfail.md)
- Per‑TU registration wrappers (CMake): [`docs/stories/003_per_tu_registration_wrappers.md`](stories/003_per_tu_registration_wrappers.md)
- Deprecate manifest + port non‑CMake builds: [`docs/stories/004_deprecate_manifest_mode_and_port_non_cmake_builds.md`](stories/004_deprecate_manifest_mode_and_port_non_cmake_builds.md)
- Non-CMake full parity for modules and explicit mocks: [`docs/stories/015_non_cmake_full_parity.md`](stories/015_non_cmake_full_parity.md)
- Parallel `gentest_codegen` jobs (TU mode): [`docs/stories/005_codegen_parallelism_jobs.md`](stories/005_codegen_parallelism_jobs.md)
- Runtime regressions and CMake check hardening: [`docs/stories/006_runtime_regressions_and_cmake_checks.md`](stories/006_runtime_regressions_and_cmake_checks.md)
- Runner modularization plan synthesis: [`docs/stories/007_runner_impl_modularization_plan.md`](stories/007_runner_impl_modularization_plan.md)
- Runner modularization execution design: [`docs/stories/008_runner_impl_modularization_design.md`](stories/008_runner_impl_modularization_design.md)
- Module mock bootstrap options (historical context): [`docs/stories/013_module_mock_bootstrap_options.md`](stories/013_module_mock_bootstrap_options.md)
- Explicit mock target codegen: [`docs/stories/014_explicit_mock_target_codegen.md`](stories/014_explicit_mock_target_codegen.md)
- Codegen-owned artifact manifest and same-module registration: [`docs/stories/034_codegen_owned_artifact_manifest_and_module_registration.md`](stories/034_codegen_owned_artifact_manifest_and_module_registration.md)
