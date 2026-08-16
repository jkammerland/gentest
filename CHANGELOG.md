# Changelog

## Unreleased

Unstable inside. No backward compatibility.

### Added

- Third-party mock backends.
- CMake-only mock generation.
- CMake module registration support.
- Ordinary importer registration units for exported module cases.
- Generated artifact manifests.
- Artifact manifest validation command.
- Module registration CLI options.
- Additive header-declaration registration for ordinary textual targets.
- Unsupported module input diagnostics.
- Module registration regression coverage.

### Changed

- Bazel consumes generated registrations.
- Xmake uses explicit module names.
- Meson emits generated-output depfiles.
- Non-CMake artifacts match codegen contracts.
- CMake classifies modules from metadata.
- Installed packages include gentest_codegen.
- AppleClang requires Xcode 26.
- Named-module registration now requires importer-reachable exported cases,
  fixtures, and adapter dependencies. Direct module-owned mock attachments are
  rejected; publish and import an explicit mock-provider target instead.
- Ordinary textual targets retain their authored sources and exact compilation
  database entries. Codegen appends generated registration sources instead of
  replacing authored sources with wrappers.
- Named-module codegen always uses `clang-scan-deps`; the previous
  `OFF`/`ON`/`AUTO` policy and source-scan fallback are removed.
- Codegen no longer hard-fails when `clang-scan-deps` is configured but
  fails and the run contains no named modules. The scanner failure is now
  reported as an info log and only becomes an error when named-module
  dependency resolution is actually required.

### Removed

- Ordinary textual-wrapper source replacement, including
  `GENTEST_INTERNAL_TEXTUAL_WRAPPER_COMPATIBILITY`,
  `--textual-wrapper-output`, and `--artifact-owner-source`.
- `GENTEST_CODEGEN_SCAN_DEPS_MODE`, `--scan-deps-mode`, and the
  `GENTEST_CODEGEN_SCAN_DEPS_MODE` environment override.

### Fixed

- Meson forwards fmt include directories.

### Documentation

- Added artifact manifest design story.
- Added changelog release notes.

### Limitations

- Ordinary textual annotations must be header-reachable; source-only,
  `static`, and anonymous-namespace cases are rejected.
- Private module fragments are unsupported.
- Module partitions are unsupported.
- Meson remains textual only.
- CMake modules need Ninja.
- CMake modules need module-capable compilers.
