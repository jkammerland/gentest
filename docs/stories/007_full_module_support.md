# Story: full module support execution roadmap

## Status
- Dependencies:
  - Land `gentest_attach_codegen()` and `gentest_codegen` module options together (`MODULE_POLICY`, `MODULE_REGISTRATION`, `MODULE_OUTPUT_DIR` and matching CLI flags).
  - Keep per-TU mode on single-config generators; continue manifest mode for multi-config environments.
  - Enable module dependency scanning for targets that compile generated mock/module imports (`CXX_SCAN_FOR_MODULES ON`).
- Expected CI impact:
  - Phase-in is additive first: new module tests, labels, presets, and non-blocking canary lanes.
  - Required lane count increases after stabilization (module smoke/full debug, then release smoke).
  - Legacy header consumer coverage remains required throughout.

## Phase 1 - Module policy and unit metadata plumbing
- Objective:
  - Introduce module-aware policy/strategy controls without changing default behavior.
- Files/components touched:
  - `cmake/GentestCodegen.cmake`
  - `tools/src/main.cpp`
  - `tools/src/model.hpp`
  - `tools/src/args.*` (or equivalent CLI parsing unit)
- Acceptance criteria:
  - CMake accepts `MODULE_POLICY <REJECT|AUTO|REQUIRE>`, `MODULE_REGISTRATION <INCLUDE|IMPORT|AUTO>`, `MODULE_OUTPUT_DIR`.
  - Codegen accepts `--module-policy`, `--module-registration`, `--unit-map`.
  - Default remains compatibility-safe (`REJECT`) with no legacy behavior drift.
- Risk notes:
  - Option mismatch between CMake and CLI can create hard-to-diagnose configure/build failures.
  - Early metadata schema churn can destabilize later phases.
- Rollback strategy:
  - Keep module path opt-in only; revert to existing invocation by forcing `MODULE_POLICY=REJECT`.

## Phase 2 - Module-aware registration generation (TU-wrapper + hybrid manifest)
- Objective:
  - Deliver include-first module support for registration in both default TU-wrapper flow and manifest flow.
- Files/components touched:
  - `cmake/GentestCodegen.cmake`
  - `tools/src/discovery.cpp`
  - `tools/src/emit.cpp`
  - `tools/src/templates.hpp`
- Acceptance criteria:
  - TU-wrapper mode generates shim units for named modules and replaces originals in `SOURCES` plus relevant `CXX_MODULE_SET*` file sets.
  - Manifest mode supports mixed inputs by allowing `--output` and `--tu-out-dir` together.
  - Include strategy works for non-exported tests/fixtures and preserves current runtime semantics.
- Risk notes:
  - Source replacement regressions can break module file-set wiring or per-source properties.
  - Hybrid output path can introduce output-name collisions.
- Rollback strategy:
  - Disable module-aware routing by policy (`REJECT`) and fall back to current non-module manifest/TU behavior.

## Phase 3 - Module-aware mocks via import block
- Objective:
  - Support `gentest::mock<T>` for exported named-module types without including module interface source files.
- Files/components touched:
  - `tools/src/mock_discovery.cpp`
  - `tools/src/render_mocks.cpp`
  - `tools/src/model.hpp`
  - `tools/src/emit.cpp`
  - `include/gentest/mock.h`
  - `cmake/GentestCodegen.cmake`
- Acceptance criteria:
  - New `<target>_mock_imports.hpp` is generated and wired before registry/impl includes.
  - Exported module-defined mock targets pass; unexported module targets fail with stable diagnostics.
  - Header-defined mocks remain behavior-compatible.
- Risk notes:
  - Missing module scan configuration can fail import resolution at compile time.
  - Import/include ordering bugs can produce subtle template specialization errors.
- Rollback strategy:
  - Provide/retain legacy header-only mock mode and use it as immediate fallback for affected targets.

## Phase 4 - Test and script coverage expansion
- Objective:
  - Add complete automated coverage for module policy, generation modes, mock behavior, regressions, and downstream package codegen usage.
- Files/components touched:
  - `tests/CMakeLists.txt`
  - `tests/module_codegen/*`
  - `tests/modules_include/*`
  - `tests/modules_import/*`
  - `tests/modules_hybrid_manifest/*`
  - `tests/modules_legacy_guard/*`
  - `tests/consumer_codegen/*`
  - `tests/regressions/*`
  - `cmake/CheckTuWrapperNamedModuleAuto.cmake`
  - `cmake/CheckManifestNamedModuleHybrid.cmake`
  - `cmake/CheckModuleRegistrationImportPolicy.cmake`
  - `cmake/CheckModuleOutputDirCollision.cmake`
- Acceptance criteria:
  - New CTest inventory from `results/full_modules/tests.md` is registered and green.
  - `*_counts`, `*_list_counts`, and `*_list_tests_lines` updates are consistent with new suites.
  - Legacy-only checks remain green and unchanged in intent.
- Risk notes:
  - Count/list assertions are brittle and often fail during incremental landing.
  - Module tests may show platform-specific instability (especially Windows/toolchain variance).
- Rollback strategy:
  - Gate new module suites behind existing module/package options and temporarily disable only failing module checks.

## Phase 5 - Package and CI rollout (module-first, compatibility-preserving)
- Objective:
  - Promote module consumer coverage to required CI signals while keeping legacy header consumer lanes permanently required.
- Files/components touched:
  - `CMakePresets.json`
  - `tests/CMakeLists.txt`
  - `cmake/CheckPackageConsumer.cmake`
  - CI workflow files under `.github/workflows/`
- Acceptance criteria:
  - Add/select presets for `debug-system-modules`, `release-system-modules`, and package-only module test execution.
  - Package tests labeled for selective gating (`package;legacy`, `package;modules`) with isolated work dirs.
  - CI rollout follows canary -> required progression for module smoke/full lanes.
- Risk notes:
  - CI duration and flake rates can exceed acceptable thresholds during early promotion.
  - Cross-platform package/module behavior can diverge.
- Rollback strategy:
  - Demote failing module lanes to non-blocking, optionally disable `GENTEST_ENABLE_PACKAGE_MODULE_TESTS` in affected lanes, keep legacy required lanes untouched.

## Phase 6 - Default flip, hardening, and documentation closure
- Objective:
  - Flip module policy default to `AUTO` only after stability gates are met, then close with updated docs and retained compatibility tests.
- Files/components touched:
  - `cmake/GentestCodegen.cmake`
  - `tests/CMakeLists.txt`
  - `tests/cmake/*` module policy checks
  - `docs/stories/007_full_module_support.md`
  - `README.md` / module-related docs as needed
- Acceptance criteria:
  - Promotion gates met: consecutive green runs, acceptable flake rate, and bounded runtime growth.
  - Legacy rejection behavior remains testable via explicit `MODULE_POLICY=REJECT`.
  - User-facing docs reflect module-first rollout and fallback controls.
- Risk notes:
  - Default flip may surprise downstreams implicitly relying on reject behavior.
  - Documentation lag can cause configuration misuse.
- Rollback strategy:
  - Revert default to `REJECT` while keeping module support available via explicit `MODULE_POLICY=AUTO`.
