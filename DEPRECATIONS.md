# Deprecated and removed features

This page lists the gentest features to stop using and what replaces each one.
Other changes in each release are in [CHANGELOG.md](CHANGELOG.md).

- A **deprecated** feature still works, but a later release may remove it.
- A **removed** feature is gone. Using it fails with an error that names the
  replacement, unless the table says otherwise.

## Deprecated

| Feature | Deprecated in | Status | Use instead |
| --- | --- | --- | --- |
| Member tests: annotated member functions of a fixture class | 2.0.0 | Works, without a warning. Removal is not scheduled. | Free-function tests, benchmarks, and jitters that take their fixtures as parameters. See [Fixtures](docs/fixtures.md). |
| The `GENTEST_STRICT_FIXTURE` environment variable, read by `gentest_codegen` | 1.0.0 | Works, without a warning. Removal is not scheduled. | `gentest_codegen --strict-fixture`, or the `STRICT_FIXTURE` option of `gentest_attach_codegen()`. |

Strict-fixture mode makes codegen reject member tests in classes marked
`fixture(suite)` or `fixture(global)`.

## Removed

"Removed in" is the first release without the feature.

| Feature | Removed in | What happens if you use it | Use instead |
| --- | --- | --- | --- |
| `gentest::async_test<T>` | 2.0.0 | Compile error. | `gentest::awaitable<T>`. See [Async tests](docs/async.md). |
| Timed-wait hooks on gentest's 1.x awaitables: the `gentest_async_wait_supported` member type and the `await_suspend_with_token` and `cancel_wait` members | 2.0.0 | Compile error. | Nothing. `wait_for` and `wait_until` take ordinary C++ awaiters. |
| The `GENTEST_USE_BOOST_JSON` CMake option and the `allure-boost-json` vcpkg feature | 2.0.0 | The option prints a deprecation message and has no effect. The vcpkg feature doesn't exist. | Nothing. The Allure writer is always built and doesn't need Boost.JSON. |
| The `time` field in Allure results | 2.0.0 | Allure results don't contain it. | The `start` and `stop` fields. See [Reports](docs/reports.md). |
| The namespace attribute `suite("...")` | 2.0.0 | Codegen fails with an error saying `namespace attribute 'suite' was removed in gentest 2.0.0` and naming the replacements. | The namespace path, which is always the suite. Group cases with `test("...")` names or in separate test executables, or rename the namespace to the suite name you want. See [Writing tests](docs/writing_tests.md). |
| `gentest_attach_codegen(... OUTPUT <file>)` | 1.0.0 | Configure fails. | Omit it; header registration is the default. Pass `OUTPUT_DIR <dir>` to choose where generated files go. |
| `gentest_attach_codegen(... NO_INCLUDE_SOURCES)` | 1.0.0 | Configure fails. | Nothing. Header registration compiles your sources unchanged. |
| `gentest_codegen --output <file>` and `--template <file>` | 1.0.0 | Codegen exits with an error. | `--tu-out-dir` with one registration output per input. See [Codegen artifact protocols](docs/codegen_artifact_protocols.md). |
| `gentest_codegen --no-include-sources` and the `GENTEST_NO_INCLUDE_SOURCES` environment variable | 1.0.0 | Codegen exits with an error. | Nothing. Header registration compiles your sources unchanged. |
| `EXPECT_SUBSTRING` in `gentest_discover_tests()` | 1.0.0 | Configure fails. | `DEATH_EXPECT_SUBSTRING`. See [CTest integration](docs/discover_tests.md). |
| The `fixtures(...)` attribute | 1.0.0 | Codegen fails with `'fixtures(...)' was removed; infer fixtures from function parameters instead`. | Fixture parameters in the function signature. See [Fixtures](docs/fixtures.md). |
| Runner option `--run-test=<name>` | 1.0.0 | Usage error; the runner exits with code 1. | `--run=<name>`. See [Command-line options](docs/command_line.md). |
| Runner options `--run-bench=<name>` and `--bench-filter=<pattern>` | 1.0.0 | Usage error; the runner exits with code 1. | `--run=<name> --kind=bench` and `--filter=<pattern> --kind=bench`. |
| Runner options `--run-jitter=<name>` and `--jitter-filter=<pattern>` | 1.0.0 | Usage error; the runner exits with code 1. | `--run=<name> --kind=jitter` and `--filter=<pattern> --kind=jitter`. |
| `gentest_codegen --scan-deps-mode` and the `GENTEST_CODEGEN_SCAN_DEPS_MODE` CMake and environment variable | 1.0.0 | Codegen rejects the option as unknown. The variable is ignored. | Nothing. Named-module codegen always uses `clang-scan-deps`. |
| Internal wrapper options: `gentest_codegen --textual-wrapper-output` and `--artifact-owner-source`, and `GENTEST_INTERNAL_TEXTUAL_WRAPPER_COMPATIBILITY` | 1.0.0 | Codegen rejects the options as unknown. The variable is ignored. | Header registration: annotate declarations in headers, and codegen adds registration sources to the target. |

The error messages for `OUTPUT`, `NO_INCLUDE_SOURCES`, `--output`, `--template`,
`--no-include-sources`, and `EXPECT_SUBSTRING` say the feature was removed in
gentest 2.0.0. Every release since 1.0.0 rejects them with those messages.

## Internal cleanup

These internal helpers were removed before 1.0.0. They were never public API.
They are listed here so that maintainers keep them from coming back.

| Removed item | Replaced by |
| --- | --- |
| The installed `share/cmake/gentest/scan_inspector/` helper directory | `gentest_codegen --inspect-source`, and validation of mock and artifact manifests. The package-consumer test fails if an install contains this directory (`tests/cmake/scripts/CheckPackageConsumer.cmake`). |
| Configure-time source-inspection probes in CMake | Codegen classifies sources and named modules itself. Mock manifests carry module data, and codegen writes the mock aggregate modules and the artifact manifests. |
| CMake helpers that collected include directories and macros for scans | Codegen reads `compile_commands.json`. |
| The `meson/*.in` generated-source skeletons | Codegen writes the `--textual-registration-output` and `--mock-public-header` files directly. `meson/textual/mock_anchor.cpp.in`, a one-line source that includes the generated mock header, remains. |
| The `xmake/templates/*.in` generated-source skeletons | Codegen writes the registration sources directly. |
| Wrapper-source planning in Xmake and Bazel | Both compile your sources unchanged and add the generated registration sources. |

Design history: the [codegen cleanup notes](docs/stories/037_codegen_contract_cleanup_campaign.md)
and [issue #115](https://github.com/jkammerland/gentest/issues/115).
