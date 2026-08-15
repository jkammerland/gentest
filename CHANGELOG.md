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
- Textual wrapper artifact manifests.
- Module registration CLI options.
- Textual owner-source CLI option.
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

### Fixed

- Meson forwards fmt include directories.

### Documentation

- Added artifact manifest design story.
- Added changelog release notes.

### Limitations

- Textual registration still uses wrappers.
- Private module fragments are unsupported.
- Module partitions are unsupported.
- Meson remains textual only.
- CMake modules need Ninja.
- CMake modules need module-capable compilers.
