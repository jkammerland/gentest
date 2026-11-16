# Design notes

These design notes ("stories") record the goals, options and decisions made
while gentest was built. They are kept as history and their bodies aren't
updated when the code changes. The user documentation in the
[documentation index](../index.md) is authoritative; removed features are listed
in [DEPRECATIONS.md](../../DEPRECATIONS.md).

## Status meanings

Each note starts with a status block that describes it as of gentest 2.0.0
and gives the specifics. The table uses these statuses:

- **Implemented in *X***: the design shipped in release *X*.
- **Partly implemented**: part of the plan shipped.
- **Fixed in *X***: the note investigates a bug, and the fix shipped in release
  *X*.
- **Done**: the planned work (an inventory, triage or cleanup campaign) is
  finished.
- **Superseded** or **partly superseded**: a later design replaced all or part
  of the note (see "Superseded by"). "Before 1.0.0" means the replaced design
  never shipped in a release.
- **Unresolved**: the problem the note describes has no fix.
- **Historical record**: data captured once, as a companion to another note.
- **Removed**: the file was deleted; see [Removed files](#removed-files).

## All notes

| # | Design note | Status | Superseded by | Current docs | Details |
|---|---|---|---|---|---|
| 001 | [P0 correctness fixes and regression tests](001_p0_correctness_and_regression_tests.md) | Implemented in 1.0.0 | — | [Assertions](../assertions.md), [Failure diagnostics](../failure_diagnostics.md), [Mocks](../mock_generation.md) | Fixes for `Approx::rel()`, logging, option parsing, `returns_ref()` and null C strings. |
| 002 | [Runtime `skip()` and `xfail()` (expected failure)](002_runtime_skip_and_xfail.md) | Implemented in 1.0.0 | — | [Assertions](../assertions.md), [Reports](../reports.md) | The proposed `XFailScope` was not added. |
| 003 | [Per-translation-unit registration wrappers (CMake)](003_per_tu_registration_wrappers.md) | Superseded before 1.0.0 | Header registration ([story 036](036_textual_declaration_only_registration.md)) | [Writing tests](../writing_tests.md) | Header registration replaced the shim translation units. |
| 004 | [Deprecate manifest mode and port non-CMake builds to per-translation-unit generation](004_deprecate_manifest_mode_and_port_non_cmake_builds.md) | Phase 1 implemented in 1.0.0 | — | [Choosing a build system](../buildsystems/downstream_consumers.md), [Meson](../buildsystems/meson.md), [Xmake](../buildsystems/xmake.md), [Bazel](../buildsystems/bazel.md) | Later phases moved to [story 015](015_non_cmake_full_parity.md); manifest mode removed before 1.0.0. |
| 005 | [Parallel `gentest_codegen` scan slots](005_codegen_parallelism_jobs.md) | Implemented in 1.0.0; kept current | — | This note; summary in [AGENTS.md](../../AGENTS.md) | Kept current: describes the 2.0.0 `--jobs` design. |
| 006 | [Runtime regression hardening and CMake check script updates](006_runtime_regressions_and_cmake_checks.md) | Implemented in 1.0.0 | — | [Fixtures](../fixtures.md), [Benchmarks and jitter](../benchmarks.md), [Contributing to gentest](../../CONTRIBUTING.md) | The regressions run in CTest; the script arguments changed. |
| 007 | [Modularize `runner_impl` and harden pre-refactor regression coverage](007_runner_impl_modularization_plan.md) | Implemented in 1.0.0 | — | None (internal design) | Plan and regression backlog for [story 008](008_runner_impl_modularization_design.md). |
| 008 | [`runner_impl` modularization (execution, fixtures and reporting)](008_runner_impl_modularization_design.md) | Implemented in 1.0.0; decision 2 reversed in 2.0.0 | — | [Fixtures](../fixtures.md) | Shared fixtures are set up when a selected case first needs them. |
| 009 | [Public modules end to end](009_public_modules_end_to_end.md) | Partly superseded before 1.0.0 | Module registration ([issue #114](https://github.com/jkammerland/gentest/issues/114)); mock targets ([story 014](014_explicit_mock_target_codegen.md)) | [Named modules](../modules.md) | The public modules shipped; module-wrapper registration did not. |
| 010 | [Public modules progress report](010_public_modules_progress_report.md) | Superseded before 1.0.0 | Mock targets ([story 014](014_explicit_mock_target_codegen.md)); module registration ([issue #114](https://github.com/jkammerland/gentest/issues/114)) | [Named modules](../modules.md), [Mocks](../mock_generation.md) | Progress report on the module-wrapper design of [story 009](009_public_modules_end_to_end.md). |
| 011 | PR review to-do list | Removed | — | — | A branch-review to-do list, not a design note. |
| 012 | [Libclang musl parallel parse crash](012_libclang_musl_parallel_parse_crash.md) | Fixed in 1.0.0 | — | [Story 005](005_codegen_parallelism_jobs.md) | Larger default codegen thread stack on Linux; serial parsing can be forced. |
| 013 | [Module mock bootstrap options](013_module_mock_bootstrap_options.md) | Superseded | [Story 014](014_explicit_mock_target_codegen.md) | [Mocks](../mock_generation.md), [Named modules](../modules.md) | Story 014 built option 2 as mock targets. |
| 014 | [Explicit mock target codegen](014_explicit_mock_target_codegen.md) | Implemented in 1.0.0 | — | [Mocks](../mock_generation.md) | Shipped as `gentest_add_mocks()`. |
| 015 | [Non-CMake parity for explicit mocks and supported module paths](015_non_cmake_full_parity.md) | Implemented in 1.0.0 for the supported scope | — | [Choosing a build system](../buildsystems/downstream_consumers.md), [Meson](../buildsystems/meson.md), [Xmake](../buildsystems/xmake.md), [Bazel](../buildsystems/bazel.md) | Meson has no named modules or module mocks. |
| 016 | [Explicit host-Clang contract for `gentest_codegen`](016_explicit_host_clang_codegen_contract.md) | Implemented in 1.0.0; Bazel part superseded | Bazel's [exec toolchain](../buildsystems/bazel.md#exec-toolchain-contract) | [Host toolchains and cross-compiling](../buildsystems/host_toolchain_sysroots.md) | Bazel uses the exec toolchain instead. |
| 017 | [Exported library mock packages for downstream consumers](017_exported_library_mock_packages.md) | Partly implemented | — | [Mocks](../mock_generation.md), [Named modules](../modules.md) | Mock targets install and export; the library-owned layout was not built. |
| 018 | [Windows LLVM 20 MatchFinder traversal crash in module codegen](018_windows_llvm20_matchfinder_scope_failure.md) | Unresolved (Windows LLVM 20) | — | None (internal design) | Windows CI tests LLVM 21 and newer. |
| 019 | [Replace chained `std::string::append()` assembly with `fmt`](019_fmt_string_assembly_cleanup.md) | Partly implemented in 1.0.0 | — | None (internal code style) | Chained appends remain in two code generator files. |
| 020 | [Official downstream support for Bazel, Xmake/xrepo and Meson](020_official_non_cmake_downstream_support.md) | Implemented in 1.0.0 | — | [Choosing a build system](../buildsystems/downstream_consumers.md) | Consumer fixtures live under `tests/downstream/`. |
| 021 | [Deduplicate runtime test-context plumbing and generated output naming](021_runtime_context_and_codegen_dedup.md) | Implemented in 1.0.0 | — | None (internal change) | Output naming continued in [story 029](029_codegen_output_naming_contract_unification.md). |
| 022 | [Refresh the native Windows failure inventory and split concrete follow-ups](022_windows_ci_portability_followups.md) | Done before 1.0.0 | — | [Windows troubleshooting](../windows_troubleshooting.md) | Triage of one Windows test run; later stories fixed each failure. |
| 022 | [Native Windows inventory for story 022](022_windows_ci_portability_followups_inventory.md) | Historical record (2026-04-14) | — | [Windows troubleshooting](../windows_troubleshooting.md) | One native Windows run: 501 tests, 5 failed. |
| 023 | [Reduce installed public API leakage of runtime internals](023_public_api_internal_surface_reduction.md) | Implemented in 1.0.0 | — | [Embedding the runner](../runner_api.md) | `gentest/runner.h` pulls in no registration or runtime-detail headers. |
| 023 | [Public API surface inventory for story 023](023_public_api_internal_surface_inventory.md) | Historical record (April and May 2026) | — | [Embedding the runner](../runner_api.md) | Matches 2.0.0 except `gentest::CurrentContext`. |
| 024 | [Unify the manifest and per-translation-unit wrapper emission pipelines](024_codegen_emission_pipeline_unification.md) | Implemented in 1.0.0 | — | None (internal change) | Manifest mode and the wrappers for test targets were later removed. |
| 025 | [Move source transformation logic out of `GentestCodegen.cmake`](025_codegen_cmake_logic_reduction.md) | Implemented in 1.0.0 | — | None (internal change) | No CMake helper reads C++ source text. |
| 026 | [Simplify test helper infrastructure and derive inventory expectations](026_test_helper_and_inventory_simplification.md) | Implemented in 1.0.0 | — | [Contributing to gentest](../../CONTRIBUTING.md) | The seven audited suites read expectations from `tests/expected/<suite>.list`. |
| 027 | [Separate core reporting from optional Allure and format sinks](027_reporting_pipeline_and_allure_split.md) | Implemented in 1.0.0 | — | [Reports](../reports.md) | Allure output split from core reporting. |
| 028 | [Make Windows path-depth and short-root behavior robust](028_windows_helper_path_depth_portability.md) | Implemented in 1.0.0 | — | [Windows troubleshooting](../windows_troubleshooting.md) | Fixed two failures from the story 022 inventory. |
| 029 | [Unify tool and CMake codegen output naming contracts](029_codegen_output_naming_contract_unification.md) | Implemented in 1.0.0 | — | [Codegen artifact protocols](../codegen_artifact_protocols.md) | CMake passes every generated file path to codegen. |
| 030 | [Simplify mock codegen model, discovery and render helpers](030_mock_codegen_model_and_render_simplification.md) | Implemented in 1.0.0 | — | None (internal change); for using mocks, [Mocks](../mock_generation.md) | Internal refactor of mock code generation. |
| 031 | [Preserve explicit scan-deps mode in installed public-module consumers](031_public_module_scan_deps_mode_contract.md) | Superseded before 1.0.0 | [PR #118](https://github.com/jkammerland/gentest/pull/118): named-module codegen always runs `clang-scan-deps` | [Named modules: requirements](../modules.md#requirements) | Named-module codegen always runs `clang-scan-deps`. |
| 032 | [Separate the generated-code devkit boundary from installed runtime detail](032_generated_runtime_devkit_boundary_cleanup.md) | Implemented in 1.0.0 | — | None (unstable internal headers); public API: [Embedding the runner](../runner_api.md) | Generated code uses unstable `detail/` headers. |
| 033 | [Split `GentestCodegen.cmake` into focused modules](033_gentest_codegen_cmake_modularization.md) | Implemented in 1.0.0 | — | None (internal change) | The public CMake functions kept their names. |
| 034 | [Codegen-owned artifact manifest and same-module registration](034_codegen_owned_artifact_manifest_and_module_registration.md) | Partly superseded before 1.0.0 | Module registration ([issue #114](https://github.com/jkammerland/gentest/issues/114)); header registration ([story 036](036_textual_declaration_only_registration.md)) | [Codegen artifact protocols](../codegen_artifact_protocols.md), [Named modules](../modules.md) | The artifact and mock manifests shipped in 1.0.0. |
| 035 | [Replace module-wrapper mock injection with split mock registration](035_module_mock_split_protocol_registration.md) | Superseded before 1.0.0 | Module registration ([issue #114](https://github.com/jkammerland/gentest/issues/114)) | [Named modules](../modules.md), [Mocks](../mock_generation.md) | A module test imports a mock target's generated mock module. |
| 036 | [Additive textual header-declaration registration](036_textual_declaration_only_registration.md) | Implemented in 1.0.0 | — | [Writing tests](../writing_tests.md), [Codegen artifact protocols](../codegen_artifact_protocols.md) | This is header registration, the default. |
| 037 | [Codegen contract cleanup campaign](037_codegen_contract_cleanup_campaign.md) | Done, except the line-count targets | In part: header registration ([story 036](036_textual_declaration_only_registration.md), [issue #115](https://github.com/jkammerland/gentest/issues/115)) | [Deprecated and removed features](../../DEPRECATIONS.md), [Codegen artifact protocols](../codegen_artifact_protocols.md) | Line-count targets not met; removed options already fail in 1.0.0. |

## Removed files

Git history keeps these files; for example,
`git show v1.3.0:docs/stories/STATUS.md` prints the status matrix.

- `STATUS.md`: the status matrix for stories 015 and 021-037, replaced by the
  table above.
- `EXECUTION_PLAN.md`: an April 2026 work plan for those stories, outdated by
  the reversal recorded in story 036.
- `011_pr_review_todo.md`: a to-do list from a branch review, not a design note.
