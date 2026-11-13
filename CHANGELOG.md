# Changelog

## 2.0.0 - Unreleased

### Upgrading

- Regenerate and rebuild with matching 2.0.0 codegen, headers, and runtime; 1.x generated sources are not compatible.
- Rename `async_test<T>` to `gentest::awaitable<T>` and replace `await_subscribe` / `cancel_wait` with ordinary C++ awaiters.
- Add `FILE_SET gentest_explicit_mock_defs DESTINATION include` when installing `gentest_add_mocks()` targets.
- Review nice-mode mocks that relied on ignored extra calls, and shared fixtures expected to set up when unused.

### Breaking changes

- Async cases, helpers, and fixture hooks return `gentest::awaitable<T = void>` instead of `async_test<T>`.
- Custom async operations are ordinary C++ awaiters; `await_subscribe` / `cancel_wait` and the callback-storage alias are removed.
- `when_any`, `wait_for`, and `wait_until` require exceptions; `awaitable` and `when_all` do not.
- The generated-registration contract is version 6 (`GENTEST_CASE_API_VERSION`), up from 4 (#238, #299).
- Shared suite and global fixtures set up on first use by a selected case, not at the start of the run (#240).
- Nice-mode native mocks fail calls beyond a method's expectations, including `.times(0)` (#261).
- Changing native mock configuration after its first call or verification records a test failure (#238).
- `gentest_add_mocks()` uses `DEFS` in place as public headers of the mock target instead of copying them (#239).
- Installing a textual mock target requires its new `gentest_explicit_mock_defs` file set.
- A `HEADER_NAME` with the same include path as a `DEFS` file is rejected.
- `DEFS` are included by their path relative to the current source or binary directory, or to the new `BASE_DIRS` argument.
- Two mock targets used by one test must not spell different `DEFS` files by the same include path; configure fails if they do.
- JUnit reports blocked cases as `<error>` and Allure as `broken`, instead of skipped.
- Duplicate single-valued runner options, `--repeat` outside 1..1000000, `--bench-epochs=0`, and `--jitter-bins` above 10000 are errors.
- `EXPECT_NO_THROW` / `EXPECT_THROW` macros (and `ASSERT_` forms) reject a lambda argument at compile time; pass a statement, or call `(EXPECT_NO_THROW)(fn)`.
- `when_all` and `when_any` are class templates used through deduction; they can no longer take explicit template arguments or have their address taken.
- Explicit stop-token overloads of `sleep_for`, `yield`, `event.wait`, and `future.wait` require exceptions.
- `--quiet-clang` / `QUIET_CLANG` no longer hides parse errors; a file that fails to parse fails codegen.
- Numeric `range` / `linspace` bounds must be numeric literals; a mocked method named `mock` and template tests without a full `template(...)` set are rejected.

### Added

- `when_all` and `when_any` compose awaitables, returning an owning tuple or variant after joining every input.
- Custom callback awaitables with typed, move-only results that resume on the owning test runner.
- Sixteen runnable async examples in eight stages.
- Failure messages show formatted values and the first mismatch for containers, strings, tuples, optionals, and variants (#243).
- `diagnostic_context(...)` scopes label failures with helper-call and loop context (#243).
- `gentest/matchers.h` shares matchers between assertions and native mocks, with `expect_that`, `require_that`, and `ElementsAre` (#243).
- Native mock failures name the expectation's declaration location (#243).
- Tag selection with `--tag`, `--exclude-tag`, `--exclude`, and `--list-tags` (#240).
- Discovery adds case tags to CTest `LABELS` (#240).
- `--output=full|compact|failures-only` and `--log-output=all|failures` (#242).
- `--break-on-failure` stops in a debugger at the failing assertion (#242).
- Failing cases print a shell-quoted command that reruns exactly that case (#242).
- Repeated shuffled runs print each iteration's seed and replay command (#242).
- `--skip-return-code=N` sets the exit status when every selected case skips (#238).
- Progress notifications and watchdogs: `gentest::notifications` and Linux-only `gentest::notifications_linux` (#209).
- Opt-in framework progress events through `RunConfiguration`, `FrameworkEventVisitor`, and `currentNotifier()` (#210).
- Experimental, opt-in Linux network harness (`GENTEST_BUILD_HARNESS=ON`); not installed.
- Linux first-use guide and developer build measurements (#237, #241).
- `RunConfiguration::suspendProgressInterval` limits framework progress records per subject (1 ms default).
- `gentest_add_mocks(... DEPENDS ...)` is documented for ordering mock codegen after generated inputs.

### Changed

- Nested awaitables and built-in waits inherit cooperative cancellation; explicit stop tokens override it.
- Deadline waits request stop on timeout and join the operation, so cleanup can extend past the deadline.
- Discovered cases report runtime skips to CTest with exit code 77 (#238).
- Discovered death tests pass only if the case body started; setup deaths and CLI errors fail (#238).
- `Approx` defines exact equality, signed zero, NaN, infinity, and zero tolerance (#238).
- Successful runs that execute no cases write a fresh zero-test JUnit report (#238).
- Benchmark and jitter loops call a body-only generated entrypoint (#299).
- Full CI runs the QEMU and libassert test lanes in parallel (#300).
- Native mock calls dispatch about twice as fast.
- JUnit `time` is written as a fixed-point decimal, independent of locale.
- Runs that fail before executing any case replace a stale JUnit report, and stale gentest result files are removed from `--allure-dir`.
- Mock codegen is ordered after linked library targets and after the `DEFS` files.
- `gentest_attach_codegen()` exports compile commands for its target without `CMAKE_EXPORT_COMPILE_COMMANDS`.
- The release workflow checks for a dated changelog heading and packages once before pushing the tag.

### Fixed

- Codegen depfiles escape literal dollar signs (#213).
- Stale and competing async callbacks can no longer publish into a later suspension.
- Coroutine frames and composition inputs stay alive during reentrant stop callbacks.
- Fetched static fmt is built position-independent for the clang-tidy plugin.
- An unmet mock expectation in a bench or jitter body fails the case instead of terminating the run (#262).
- A production header using only `#pragma once` can be included alongside the mock header that mocks it (#239).
- Very large durations and time points saturate instead of overflowing into an immediate timeout (#248).
- `--fail-fast` no longer hangs when a case polls with `yield()` (#263).
- Completed waiter tokens are pruned, bounding memory in long async cases (#264).
- Measured runs whose matches are all excluded death tests rewrite stale reports (#252).
- Per-TU test registrars have internal linkage, so linked codegen targets keep their cases (#260).
- Codegen no longer emits reserved identifiers into user code (#267).
- Generated mock signatures globally qualify every nested name (#265).
- Generated mocks keep `noexcept(expr)` on class template specializations (#266).
- Generated mocks handle function-pointer and array-reference parameters and returns (#276).
- `--filter` and `--exclude` match names containing a literal `*`.
- Compile flags containing ` || `, ` && `, ` | ` or ` ; ` no longer truncate the codegen compile command.
- Quotes and `//` in skipped or commented text, and `inline` member tests, no longer hide later test attributes.
- Global-namespace fixtures, `long double` / hex / exponent axis bounds, and string parameter escapes generate correct code.
- Template tests bind fixture parameters to their template arguments, and member template tests keep their method name.
- Benchmarks returning a reference no longer copy the result inside the timed loop.
- Duplicate display names are rejected, and placeholder-like text in names or parameters is left intact.
- Module sources with `/*` or `//` inside string literals are recognised; `.inl`, `.ipp`, and `.tpp` headers can be scanned alone.
- Native mocks distinguish methods of different base classes instead of sharing one expectation slot.
- Native mocks compile for parameters without `operator==`, abstract parameter types, non-default-constructible returns, overloads, and parameter packs.
- `.with()` owns text arguments and compares `const char*` by content; stateful and out-parameter `invokes` actions work.
- Generated native mocks compile with GCC for inherited virtual methods, and with parameters named `token`.
- GMock and Trompeloeil default-argument overloads forward move-only and rvalue arguments.
- The fmt-message assertion overloads compile and report the caller's location.
- `EXPECT_THROW` works without RTTI, byte pointers print as addresses, and `record_cbor.h` compiles without exceptions.
- Failure paths are trimmed at path-component boundaries.
- Hand-written death-tagged benchmarks report body entry instead of terminating.
- Allure results sanitise invalid UTF-8.
- Rerun commands printed for PowerShell quote typographic single quotes.
- Polling with `yield()` under the Linux notification transport no longer overflows the progress queue.
- Completed or cancelled timers are released without waiting for an earlier timer.
- The Linux notifier leaves inherited descriptors untouched when they fail validation.
- A newer producer generation retires the older generation's watchdog subjects.
- A process that exits right after its final notification is not reported as exited early.
- `--help` prints the real version in every build system.
- gentest no longer forces the MSVC runtime into a consumer's cache.
- Installs honour `CMAKE_INSTALL_DATADIR`, find `gentest_codegen` in their own prefix, and do not export the Homebrew libc++ path.
- Bazel `gentest_attach_codegen_textual(main = ...)` links the runtime; Meson passes the fmt fallback include to codegen.
- The clang-tidy gate, `ctest -R` steps, and negative codegen checks fail when they check nothing.

## 1.3.0 - 2026-10-18

### Added

- One-shot benchmarks with `bench_once`, application-supplied primary durations
  with `bench_time("supplied")`, named timing distributions, and owning
  measurement snapshots available during local fixture teardown.
- Measured reports include execution and timing policies, named summaries, and
  P99 statistics. Comparisons identify incompatible timing or sampling policies.
- `--list-selected` previews the execution selection without constructing
  fixtures. `--fail-on-empty` and CTest discovery's `FAIL_ON_EMPTY` option can
  reject empty selections before fixture setup.
- Maintainers can request complete PR validation with `/full-ci`, a bot
  checkbox, or manual dispatch. Requests validate permissions and pin the
  candidate, base, and merge revisions.

### Changed

- Repository compiler and lint tooling consistently select LLVM 23. Older LLVM
  versions remain explicit compatibility lanes in full CI.
- Package integration tests share immutable installed-package fixtures. Full CI
  preserves the supported OS, compiler, build-system, sanitizer, and QEMU lanes.
- Coverage diagnostics are generated after completed tests even when tests fail;
  incomplete coverage still fails the existing coverage gates.

### Fixed

- Machine-readable reports route fixture diagnostics to stderr, preserving
  parseable report output on stdout.
- Fatal analyzer assertion macros support MSVC's traditional preprocessor.
- CMake discovery honors filters whose text resembles a false CMake value.
- Installed module providers preserve importer preprocessing context, and
  codegen searches target system includes before host fallback directories.
- Host LLVM tools and SDK packages are selected coherently. Imported-library
  configuration mappings, stale Windows DIA dependencies, and inherited scanner
  settings are handled consistently across compatibility toolchains.
- Cross-compiled test helpers preserve target execution and emulator metadata.
- Parallel benchmark and jitter Allure checks use independent report directories;
  report-isolation validation handles single-config and multi-config generators.

## 1.2.2 - 2026-10-04

### Changed

- Every pull request automatically runs Clang Format and Clang Tidy, including
  draft PRs and documentation-only changes. Full CI remains available through
  the Actions button or a command that selects a PR's branch and base commit.
- Windows Clang and MSVC CI validation use `RelWithDebInfo` consistently for
  configure, build, and test steps.

### Fixed

- Artifact-manifest validation uses response files to avoid Windows command-line
  length limits while preserving quoted paths and arguments.
- Response-file argument storage reserves its known capacity, fixing the
  clang-tidy error that caused master CI to fail.
- Updating or publishing a release draft preserves its version tag and target
  commit instead of resetting the tag to an untagged placeholder.

## 1.2.1 - 2026-10-02

### Changed

- Public releases now publish signed source TGZ/ZIP archives for every OS instead
  of Linux-only LLVM-bound host developer kits. Source archives include the
  runtime and codegen sources, checksums, and commit/tool provenance. Portable
  codegen binaries per OS/architecture remain tracked in #136.

### Fixed

- Codegen builds with GCC 16 in C++26 mode using a typed delimiter lookup.
- Release titles match their version tags, and the README CI badge tracks the
  current master workflow.
- Jitter summaries expose sampling mode and calls per sample, label batch
  maxima and histograms, and warn when averaging can hide individual-call
  latency spikes. JSON/CSV and Allure exports carry the sampling metadata.

## 1.2.0 - 2026-09-29

### Added

- Optional Glaze JSON and cbor_tags CBOR recording adapters, pinned vcpkg
  features, and an executable runtime recording example.
- Runtime scalar properties and owned structured records with case, suite, and run
  scopes, versioned record bundles, JUnit properties, and Allure attachments.
- Executable examples for parameterized cases, fixtures, mocks, measured results,
  and metadata exports.
- A private local validation runner and report collector for the exhaustive
  platform suite, with a manually dispatched full GitHub CI profile.

### Changed

- Recording, inventory, and measured reports share one UTF-8-safe JSON string
  encoder.

- Transitional release archives are explicitly named as LLVM-bound host
  developer kits and carry a validated, machine-readable artifact contract.
- Standalone release manifests and SBOMs use artifact-scoped names so future
  source SDK and platform-codegen assets can coexist without collisions.
- Pull-request CI starts only when manually dispatched; master pushes run a
  smaller routine bundle.

### Fixed

- Runs without report exporters release completed synchronous and asynchronous
  case recordings instead of retaining a history for every repetition.
- Recording reports share immutable payload storage across cases and Allure
  writes, avoiding repeated copies of run and suite payloads.
- JUnit preserves whitespace in recorded property keys and values; recording
  verification reads UTF-8 consistently across host locales.

- Draft publication resumes through its numeric GitHub release ID, avoiding
  tag-based draft lookups that return 404 after successful creation.
- Codegen accepts LLVM 23's relocated USR-generation header and component while
  retaining compatibility with LLVM 20–22.
- Codegen preserves compiler delimiters, fallback-header semantics, and
  clang-cl/MSVC driver defaults, restoring strict Windows builds.
- CMake discovery preserves punctuation in case names and rejects death tests
  whose process could not launch.
- Mock dispatch preserves declared rvalue-reference argument types, and measured
  JSON replaces invalid UTF-8 consistently.
- Xmake resolves the Gentest root even when an earlier prefix candidate is
  unavailable (for example, from a shallow checkout under `/tmp`).

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

- Generated shared-fixture failure handling stays in its templated caller,
  avoiding an unneeded internal function under clang-cl `/W4 /WX`.
- Codegen parses function bodies normally, avoiding false unused-private-field
  diagnostics under warnings-as-errors while preserving genuine body errors.
- Function-pointer assertion and mock diagnostics use explicit boolean
  conversion instead of MSVC's function-to-object pointer extension, preserving
  stream manipulators and custom insertion operators under warnings-as-errors.
- Compile-command delimiter parsing follows the selected Clang driver mode,
  keeping clang-cl flags such as `-J` from consuming the delimiter as an
  option value from another driver's grammar.
- Listed-header codegen preserves header semantics when borrowing a source-mode
  compile command, so `#pragma once` remains effective under clang-cl `-TP`
  without relaxing warnings-as-errors or changing authored-source parsing.
- Additive codegen no longer mistakes a compile command's end-of-options
  delimiter for a lost semantic option, fixing normal CMake/clang-cl commands
  on Windows. Literal `--` option values remain significant, and the ordered
  semantic-context preservation check remains enabled.
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
