# Changelog

## Unreleased

### Added

- Optional Glaze JSON and cbor_tags CBOR recording adapters, pinned vcpkg
  features, and an executable runtime recording example.
- Runtime scalar properties and owned structured records with case, suite, and run
  scopes, versioned record bundles, JUnit properties, and Allure attachments.

### Changed

- Transitional release archives are explicitly named as LLVM-bound host
  developer kits and carry a validated, machine-readable artifact contract.
- Standalone release manifests and SBOMs use artifact-scoped names so future
  source SDK and platform-codegen assets can coexist without collisions.

### Fixed

- Draft publication resumes through its numeric GitHub release ID, avoiding
  tag-based draft lookups that return 404 after successful creation.
- Codegen accepts LLVM 23's relocated USR-generation header and component while
  retaining compatibility with LLVM 20–22.

## 1.1.0 - 2026-08-26

### Added

- Added opt-in analyzer-aware `ASSERT_TRUE` and `ASSERT_FALSE` fatal guards
  through `gentest/analyzer_assertions.h`.

### Changed

- Generated CMake registration sources are marked `SKIP_LINTING`, keeping
  consumer static-analysis runs focused on authored sources.
- Pull-request CI avoids duplicated package-consumer work and uses a shared
  Python fixture harness for CMake regression tests.

### Fixed

- Release publication can safely resume a matching verified draft while still
  refusing to replace an already-published release.

## 1.0.0 - 2026-08-23

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
- Header-only test targets: an annotated header alone can form a test target,
  and the generated registration sources become its translation units.
  Supported by CMake, Bazel, Meson, and Xmake.

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
- Xmake textual codegen now shortens generated registration stems to a
  24-character budget (16-character prefix plus an 8-character digest);
  raw source basenames could previously exceed it.

### Removed

- Ordinary textual-wrapper source replacement, including
  `GENTEST_INTERNAL_TEXTUAL_WRAPPER_COMPATIBILITY`,
  `--textual-wrapper-output`, and `--artifact-owner-source`.
- `GENTEST_CODEGEN_SCAN_DEPS_MODE`, `--scan-deps-mode`, and the
  `GENTEST_CODEGEN_SCAN_DEPS_MODE` environment override.

### Fixed

- Meson forwards fmt include directories.
- Codegen strips MSVC `.modmap` module-mapping flags value-aware: a flag
  that takes a separate value argument only consumes the next token when it
  cannot be an option, so an orphaned `-ifcOutput` value (dropped when LLVM
  borrows a compile command for a file absent from the database) can no
  longer swallow the following `-reference` flag and fail the scan with
  "no such file or directory".
- Codegen applies the same value-aware consumption to the GNU-style module
  and dependency-scanning flags that build systems spell with a separate
  value argument (`-fmodule-file`, `-fprebuilt-module-path`, `-fmodule-mapper`,
  `-fdeps-format`, `-fdeps-file`, `-fdeps-target`,
  `-fconcepts-diagnostics-depth`). Clang's option table knows none of them in
  that spelling, so LLVM drops their values when it borrows a compile command;
  the orphaned flag no longer swallows the flag that follows it, and the
  module-argument collector no longer splices a following flag into a bogus
  `-fmodule-file=<flag>` and forwards it into later parse commands.
- Codegen hands `clang-scan-deps` the joined spelling of the module-mapping
  flags it preserves. CMake's Ninja dyndep pipeline emits
  `-fmodule-file <name>=<path>` and `-fprebuilt-module-path <dir>` with the
  value as a separate argument, which clang's option table does not accept, so
  forwarding it verbatim failed the entire dependency scan with
  "unknown argument: '-fmodule-file'" before any dependency was discovered. A
  flag left orphaned by compile-command interpolation carries no mapping and is
  now dropped rather than forwarded as a bare unknown argument.
- Codegen guards absolute POSIX paths when a `cl`-style compile command is
  used on a POSIX host: in MSVC driver mode an absolute path is misparsed by
  the cl option parser as an option -- `/Users/...` as `/U`, and `/Include/...`
  as `/I` with no diagnostic at all -- and the command fails with "no input
  files". Codegen now moves the input file to the end of every MSVC-mode
  command behind `--`, and reorders the external-module precompile tail
  (`--precompile -o <pcm> -- <source>`), so the driver always treats the paths
  as positional inputs. Relocating the input rather than inserting a separator
  in place is what covers the parse command, whose input is followed by
  `-fsyntax-only` and `-fmodule-file=` arguments.

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
