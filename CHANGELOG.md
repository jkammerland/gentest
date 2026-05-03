# Changelog

## Unreleased

Unstable inside. No backward compatibility.

### Added

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

- Bazel consumes generated registrations.
- Xmake uses explicit module names.
- Meson emits generated-output depfiles.
- Non-CMake artifacts match codegen contracts.
- CMake classifies modules from metadata.
- Installed packages include gentest_codegen.

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
