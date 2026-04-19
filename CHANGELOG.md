# Changelog

Notable user-facing changes for gentest are documented here.

This project is pre-release. Entries remain under `Unreleased` until the first
tagged release.

## Unreleased

### Added

- Added first-slice codegen-owned module registration support for CMake through
  `gentest_attach_codegen(... MODULE_REGISTRATION FILE_SET <name>)`.
- Added generated same-module registration implementation units for module test
  sources, preserving authored module interface files in the target instead of
  replacing or including them.
- Added artifact manifest generation for module registration outputs, including
  source kind, module name, registration output path, compile context ID,
  generated headers, depfile, include roots, and module-scan requirements.
- Added `gentest_codegen validate-artifact-manifest` for buildsystem-owned
  artifact manifest contract checks.
- Added textual per-TU wrapper artifact manifests with explicit owner source,
  generated wrapper/header, depfile, include root, attachment, and wrapper
  compatibility semantics.
- Added `gentest_codegen` CLI options for module registration artifacts:
  `--module-registration-output`, `--artifact-manifest`, and
  `--compile-context-id`.
- Added `gentest_codegen --artifact-owner-source` for textual TU artifact
  manifests.
- Added diagnostics for unsupported first-slice module registration inputs,
  including module partitions and private module fragments.
- Added regression coverage for module registration manifests, same-module
  generated registration sources, non-exported module fixtures/tests, CLI
  validation failures, and non-CMake module artifact contracts.

### Changed

- Updated Bazel module codegen wiring to consume generated registration sources
  and artifact manifests while keeping authored module interfaces as module
  interfaces.
- Updated Xmake module integration to use explicit module names and generated
  registration-output artifacts.
- Updated Meson textual codegen paths to emit depfiles for generated
  registration and mock outputs, and to request textual artifact manifests.
- Updated Bazel and Xmake textual codegen wiring to request artifact manifests
  from `gentest_codegen`.
- Aligned non-CMake module registration artifacts with the new codegen-owned
  artifact contract.
- Removed the standalone packaged `cmake/scan_inspector` helper. CMake
  `gentest_attach_codegen()` now classifies module inputs from buildsystem
  metadata and leaves source parsing to build-time `gentest_codegen`; explicit
  module mock definitions still use a runnable `gentest_codegen` for their
  configure-time module-surface checks.
- Native `gentest_INSTALL=ON` configurations now require and default
  `GENTEST_BUILD_CODEGEN=ON` so installed CMake packages include the
  `gentest_codegen` host tool that `GentestCodegen.cmake` resolves from the
  package prefix.

### Fixed

- Fixed Meson wrap consumer codegen include handling by forwarding `fmt` include
  directories from pkg-config or fallback dependency resolution.

### Documentation

- Added story `034_codegen_owned_artifact_manifest_and_module_registration.md`
  to document the intended boundary: build systems predeclare and wire
  artifacts, while `gentest_codegen` owns C++ source semantics and artifact
  roles.
- Added this changelog as the release-note source of truth for user-facing
  changes.

### Known Limitations

- Story `034` is only partially implemented. Mock phase splitting, textual
  standalone registration alignment, and a stable manifest schema remain future
  work.
- Private module fragments and module partitions are explicitly unsupported in
  the current module registration slice.
- Meson support remains textual-only; named-module registration is not
  supported in Meson.
- CMake module registration inherits the normal practical constraints of modern
  C++ module builds: use a module-capable compiler, a single-config generator
  such as Ninja, and native module scanning support.
