# gentest documentation

This page lists the gentest documentation by task.

## Start here

- [README](../README.md): what gentest is, a first example, requirements and the quick start.
- [Install gentest on Linux](install/linux.md): build and install gentest from source, write a first project, make a case fail and rerun it.
- [Install gentest on macOS](install/macos.md): the same steps with Homebrew LLVM, which `gentest_codegen` always needs; your tests can also use AppleClang 17 (Xcode 26), except with named modules.
- [Install gentest on Windows](install/windows.md): the same steps with an LLVM release package, using Clang or MSVC.
- [Examples](../examples/README.md): small CMake projects, from a one-header test target to one walkthrough per feature.

## Write tests

- [Writing tests](writing_tests.md): where cases live, attribute spelling, case names, tags and metadata, out-of-line definitions.
- [Assertions](assertions.md): `EXPECT_*` and `ASSERT_*` checks, exceptions, analyzer assertions, skip, xfail and static skips.
- [Failure diagnostics](failure_diagnostics.md): scoped failure context, matchers (also used by gentest mocks), mock failure locations and libassert expression assertions.
- [Formatting values](value_formatting.md): how `gentest::format_value` prints values in failure messages.
- [Parameters and templates](parameters.md): value axes, row tables, numeric ranges and template matrices.
- [Fixtures](fixtures.md): local, suite and global lifetimes, scoping, setup and teardown, activation order.
- [Fixture allocation hooks](fixtures_allocation.md): allocation hooks (`gentest_allocate`) for custom construction and ownership.
- [Death tests](death_tests.md): cases that must terminate the process, and how they run under CTest.
- [Mocks](mock_generation.md): mock targets, mock definitions headers (`DEFS`), gentest mocks with expectations and matchers, the gMock and Trompeloeil backends.
- [Logging and threads](logging_and_threads.md): `gentest::log()`, log sinks, adopting a test's context in worker threads, live output.
- [Benchmarks and jitter](benchmarks.md): measured cases, how timing works, JSON and CSV reports, comparing two reports.
- [Runtime properties and structured records](recording.md): facts and data recorded during a run, their scopes and their exports.
- [Named modules](modules.md): `import gentest;`, module registration (`MODULE_REGISTRATION`), module mocks and toolchain requirements.

## Run tests and read results

- [Command-line options](command_line.md): every runner option, exit codes and environment variables.
- [Test selection](test_selection.md): how name, kind and tag selectors combine; previews; empty selections and death tests.
- [Running, debugging, and replaying tests](runner_workflows.md): rerun commands, debugger breaks, compact output and captured logs, shuffle replay, CTest workflows.
- [CTest integration](discover_tests.md): `gentest_discover_tests()` options, labels and death-test arguments.
- [Reports](reports.md): JUnit, Allure and GitHub annotations, and how each outcome appears in them; gentest's own CDash script.
- [Test inventory](test_inventory.md): the `--list-json` inventory for IDEs, dashboards and CI tools.
- [Requirements traceability](traceability_standards.md): requirement-to-test trace matrices from `req(...)`, with links to standards sources.
- [Notifications and progress watchdogs](notifications.md): progress notifications, watchdogs and framework events.

## Integrate with your build

- [Choosing a build system](buildsystems/downstream_consumers.md): support matrix and settings for CMake, Meson, Xmake and Bazel.
- [Meson](buildsystems/meson.md): gentest as a Meson wrap or subproject; header registration only, no named modules.
- [Xmake](buildsystems/xmake.md): an installed gentest from Xmake, with the installed helper functions.
- [Bazel](buildsystems/bazel.md): the Bzlmod source package and the codegen exec toolchain.
- [Host toolchains and cross-compiling](buildsystems/host_toolchain_sysroots.md): how codegen finds its host Clang, target sysroots, cross builds of your project.
- [Embedding the runner](runner_api.md): write your own `main()` around the generated cases, or run hand-written cases (`gentest::Case`) with `gentest::run_cases`.
- [Packaging](packaging.md): release archives and their signatures, Common Package Specification (CPS) and software bill of materials (SBOM) metadata, the installed package helper, cutting a release.
- [Windows troubleshooting](windows_troubleshooting.md): LLVM discovery, missing builtin headers and other Windows build problems.

## Reference

- [Codegen artifact protocols](codegen_artifact_protocols.md): the manifests and generated outputs that a build-system integration exchanges with `gentest_codegen`.
- [Artifact manifest schema](schemas/gentest.artifact_manifest.v1.schema.json): JSON schema of `gentest.artifact_manifest.v1`.
- [Mock manifest schema](schemas/gentest.mock_manifest.v1.schema.json): JSON schema of `gentest.mock_manifest.v1`.
- [Changelog](../CHANGELOG.md): release notes. To upgrade from 1.x, start with the 2.0.0 section.
- [Deprecated and removed features](../DEPRECATIONS.md): what to use instead of each.

## Experimental

- [Async tests](async.md): coroutine cases that return `gentest::awaitable<>`, with composition, cancellation, events and user-owned loops.
- [Experimental Linux network harness](../experimental/harness/README.md): an opt-in harness, built from source only, that runs real UDP programs on one Linux machine with separate network stacks, NAT, firewall rules and traffic shaping.
- [Network harness design](../experimental/harness/DESIGN.md): how the network harness works inside.

## Contributing

- [Contributing to gentest](../CONTRIBUTING.md): build and test this repository; formatting, clang-tidy, coverage, commits and pull requests.
- [AGENTS.md](../AGENTS.md): rules for coding agents that work in this repository.
- [Continuous integration](ci_profiles.md): what runs automatically and how to request full CI.
- [Coverage hygiene and reporting](coverage_hygiene.md): what the coverage gate checks, its policy file and how CI uses it.
- [Private local validation](local_validation.md): run the exhaustive native validation privately on your own machine.
- [Measure adoption and edit/build/test costs](developer_build_measurements.md): run `scripts/bench_developer_cycle.py` and compare two revisions.
- [Cross-testing gentest under QEMU](cross_compile/linux_qemu.md): cross-compile gentest's own tests for aarch64 and riscv64 and run them under QEMU.
- [Libclang deferred diagnostic storage leak](issues/libclang_dependent_diagnostic_leak.md): a libclang memory leak seen under LeakSanitizer, with a proposed upstream patch.
- [Clang constraint-deduction failure ownership leak](issues/libclang_deduction_failure_leak.md): a second libclang leak, its standalone probe and patch direction.

## Design history

These pages record how gentest's design came about. The pages above describe
how it behaves.

- [Design notes](stories/README.md): one entry per design note, with its status and the page that documents the result.
- [Consumer compile scaling lab report](consumer_compile_scaling_lab_report.md) (archived): compile-time measurements against GoogleTest and doctest from April 2026.
- [LLVM 21 location API compatibility fix](llvm21-location-api-fix.md) (archived): how an LLVM 21 source-location change broke attribute discovery, and the fix.
- [`gentest_codegen` compiler selection](codegen_compiler_selection.md) (archived): why codegen needs the exact host Clang, with a regression post-mortem.
