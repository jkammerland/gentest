# Changelog

## Unreleased

Unstable inside. No backward compatibility.

### Added

- Experimental C++26 reflection-only registration for free tests, benchmarks, jitters, and basic metadata.
- Third-party mock backends.
- CMake-only mock generation.
- CMake module registration support.
- Same-module registration units.
- Generated artifact manifests.
- Artifact manifest validation command.
- Textual wrapper artifact manifests.
- Module registration CLI options.
- Textual owner-source CLI option.
- Unsupported module input diagnostics.
- Module registration regression coverage.

### Changed

- Install builds may explicitly omit `gentest_codegen` for runtime/reflection-only packages.
- Bazel consumes generated registrations.
- Xmake uses explicit module names.
- Meson emits generated-output depfiles.
- Non-CMake artifacts match codegen contracts.
- CMake classifies modules from metadata.
- Installed packages include gentest_codegen.
- AppleClang requires Xcode 26.

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
