# Repository Guidelines

Instructions for coding agents working in this repository.
[CONTRIBUTING.md](CONTRIBUTING.md) has the full build, lint, coverage, Windows
and CI commands; this file keeps the rules and the project facts you need while
editing.

## Project Structure & Module Organization
- Public headers live in `include/gentest/` (for example `runner.h`, `attributes.h`, `test.h`). The `gentest_runtime` target publishes them as a header file set, and `gentest::gentest` links it publicly.
- The public named-module interfaces (`gentest.cppm`, `gentest.mock.cppm`, `gentest.bench_util.cppm`) also live in `include/gentest/`. The `gentest` target exports them only when public modules are enabled.
- Runtime execution lives in `src/`: `runner_impl.cpp` (case registry and `run_all_tests`), `runner_cli.cpp` (option parsing), `runner_orchestrator.cpp` (run modes, help text, execution), `runner_test_plan.cpp` (grouping and shuffle) and `runner_fixture_runtime.cpp` (suite and global fixtures).
- Fixture allocation and ownership for generated code live in `include/gentest/detail/generated_runtime.h`, the `FixtureSetup`/`FixtureTearDown` interfaces in `include/gentest/detail/fixture_api.h`; `include/gentest/fixture.h` includes both.
- `gentest_codegen` is a Clang tooling program built from `tools/src/` (target defined in `tools/CMakeLists.txt`). It scans annotated cases and writes the generated registration sources. Its templates live in `tools/src/templates.hpp` and `tools/src/templates_mocks.hpp`.
- The CMake functions (`gentest_attach_codegen()`, `gentest_add_mocks()`, `gentest_link_mocks()`, `gentest_discover_tests()`) live in `cmake/GentestCodegen.cmake` and the files it includes from `cmake/gentest/`.
- `gentest_codegen` registers tests in one of two modes:
  - Header registration (the default for `gentest_attach_codegen(<target>)`): annotated declarations live in headers, the authored `.cpp` files stay in the target, and codegen adds one generated source per scan slot (`tu_XXXX_<stem>.header_registration.gentest.cpp`). It never includes or replaces an authored `.cpp`.
  - Module registration (`gentest_attach_codegen(<target> MODULE_REGISTRATION FILE_SET <name>)`): cases live in named-module interface units, which stay in the target. Codegen writes registration sources that import them (`tu_*.registration.gentest.cpp`) plus one header per translation unit (TU). Annotated cases and fixture types must be exported.
  - Both modes also write an artifact manifest, `<target>_<hash>.artifact_manifest.json`.
- A mock target whose `DEFS` is a mock definitions module (a named-module unit) goes through a separate generated-mock path in codegen; it is not a fallback for test registration.
- Public named modules are controlled by `GENTEST_ENABLE_PUBLIC_MODULES=AUTO|ON|OFF` (default `OFF`); set `ON` or `AUTO` explicitly when you want them.
- Test sources live in `tests/<name>/`, usually a `cases.cpp` plus the header it includes. `gentest_add_suite(<name>)` (`cmake/GentestTests.cmake`) builds `tests/<name>/cases.cpp` into `gentest_<name>_tests` linked with `gentest_main`; other test executables are defined directly in `tests/CMakeLists.txt`, and a few use `tests/support/test_entry.cpp` as their `main()`. Generated files land in the build tree, for example `build/<preset>/tests/<name>/tu_0000_cases.header_registration.gentest.cpp`.

## Architecture & Execution Model
- The codegen scan finds the attributes (`[[gentest::test]]`, `[[using gentest: ...]]`) and emits registration code that adds `gentest::Case` entries for each target.
- The runtime (`run_all_tests`) takes the registered cases, groups them by suite and fixture lifetime, runs fixture setup and teardown, then runs the test, bench and jitter wrappers.
- Suite identity comes from the namespace path and is used for ordering and fixture scoping. Namespaces take no gentest attributes.

## Fixture Model (Style + Semantics)
- Fixture lifetimes: local (per test, bench or jitter), suite (per namespace group), global (per run).
- Suite and global fixtures are registered at startup and set up when a selected runnable case first acquires them. Their dependencies are set up first. At the end of the run, each fixture that was set up is torn down once, in reverse setup order. Static skips acquire no fixtures.
- Suite and global fixtures are scoped to their declaring namespace and its descendants; declare a fixture in the common ancestor namespace of all tests that use it.
- Prefer free-function tests, benches and jitters whose fixture parameters codegen infers from the function signature. Codegen rejects the `fixtures(...)` attribute. Member tests are deprecated; don't add them. In a member test the class is the fixture: each test gets a fresh instance unless the class is marked `fixture(suite)` or `fixture(global)`, in which case the tests share one. See [DEPRECATIONS.md](DEPRECATIONS.md).
- Allocation hook: optional `static gentest_allocate()` or `static gentest_allocate(std::string_view suite)`, returning `std::unique_ptr` (custom deleter allowed), `std::shared_ptr`, or a raw pointer (adopted).
- Allocation failures (null or exception) are reported as test failures.

## Bench/Jitter Model
- Bench and jitter wrappers run in three phases: setup → call (timed loop) → teardown.
- Fixture setup and teardown must happen outside the timing loop; the call phase only invokes the benchmark.

## Shuffle Semantics
- Shuffling respects fixture grouping: within a suite, free and local tests are shuffled together, then each suite-fixture group, then each global-fixture group.
- There is no interleaving across fixture groups.

## Build, Test, and Development Commands
- Preferred (repository LLVM/Clang 23):
  - `cmake --preset=debug-system`
  - `cmake --build --preset=debug-system`
  - `ctest --preset=debug-system --output-on-failure`
- The `debug-system`/`release-system` presets turn on `GENTEST_ENABLE_PACKAGE_TESTS`; configure with `-DGENTEST_ENABLE_PACKAGE_TESTS=OFF` for faster local runs.
- Regenerate codegen: rebuild the relevant test target, for example `cmake --build --preset=debug-system --target gentest_unit_tests`.
- List tests: run the test executable, for example `build/debug-system/tests/gentest_unit_tests --list`.
- Sanitizers: `cmake --workflow --preset=alusan-system` (ASan, LSan and UBSan) and, on Linux, `cmake --workflow --preset=tsan-system`. `cmake --workflow --preset=alusan` is the vcpkg variant.
- Coverage (Linux gate for implementation files under `src/` and `tools/src/`, including their internal headers): run the commands in [CONTRIBUTING.md](CONTRIBUTING.md#coverage). Delete old `.gcda` files and the old report first, run CTest serially (`--parallel 1`), and pass the LLVM 23 `llvm-cov` with `--gcov`. Reports land in `build/coverage-system/coverage-report/` (`summary.md`, `index.html`).
- vcpkg presets (`debug`, `release`, `tidy`, `tidy-fix`, `alusan`) need `VCPKG_ROOT`; without it the toolchain file resolves to `/scripts/buildsystems/vcpkg.cmake` and configure fails:
  - `cmake --preset=debug`
  - `cmake --build --preset=debug`
  - `ctest --preset=debug --output-on-failure`
- Windows validation builds must use `RelWithDebInfo` (also pass `--config RelWithDebInfo` to builds and `-C RelWithDebInfo` to CTest). The Clang and MSVC commands are in [CONTRIBUTING.md](CONTRIBUTING.md#windows) (example LLVM `C:\Tools\llvm-23.1.0`). Run them in a Developer PowerShell, or after `Launch-VsDevShell.ps1 -Arch amd64`; `VsDevCmd.bat` called from PowerShell doesn't change the session's environment.
- Windows (dev machine):
  - Connect: `ssh ai-dev1@windows-11`
  - Repo path: `B:\repos\gentest`

## Coding Style & Naming Conventions
- Repository compiler and lint tooling use LLVM 23, selected by `scripts/llvm_toolchain.json`. Lint scripts and tidy presets reject other majors. Select an LLVM 23 installation with `GENTEST_LLVM_PREFIX` or the per-tool override variables listed in [CONTRIBUTING.md](CONTRIBUTING.md#repository-llvm-toolchain). Rows with older LLVM in the full CI matrix are compatibility tests.
- Follow `.clang-format` (LLVM-derived): 4-space indent, 140-column limit.
- Format edited C/C++ files explicitly with `"$(python3 scripts/llvm_tools.py resolve clang-format)" -i <paths...>`; there is no format preset.
- Run the repo-wide format check that the lint workflow uses with `scripts/check_clang_format.sh`.
- Run the clang-tidy check CI uses with `scripts/check_clang_tidy.sh build/debug-system` after configuring a Clang-based `debug-system` build; [CONTRIBUTING.md](CONTRIBUTING.md#static-analysis) has the configure command. A configure-only build directory is enough: the script first builds the generated targets the compile database refers to. `scripts/check_clang_tidy.sh build/debug-system fix` applies fixes. There is no clang-tidy build target.
- For the vcpkg-based static-analysis workflow, use the `tidy` / `tidy-fix` presets when you want configure/build/test wired together.
- Filenames: lowercase `snake_case`; types: `PascalCase`; functions: `camelCase`.
- Keep public symbols in the `gentest` namespace.

## Testing Guidelines
- Author annotated declarations in a header reachable from `tests/<name>/cases.cpp`; implementations may stay out of line in `cases.cpp`.
- Attach codegen in `tests/CMakeLists.txt`: `gentest_add_suite(<name>)` does it for a suite; a target you define yourself calls `gentest_attach_codegen(<target>)`.
- Executables return non-zero on any `gentest::failure`; always run `ctest` before pushing.
- Prefer free-function tests, benches and jitters with fixture parameters inferred from function signatures. Don't add member tests; they are deprecated (see Fixture Model above).
- Suite/global fixtures are scoped to their declaring namespace and its descendants; declare fixtures in the common ancestor namespace that owns the tests.
- If you add tests, update the matching expectations in `tests/CMakeLists.txt` and `tests/cmake/Regressions.cmake`: the `gentest_add_check_counts(...)` pass/fail/skip (and `LIST ... CASES`) numbers, any `gentest_add_check_inventory(...)` expectations (including the `tests/expected/*.list` files), and the count tables that loops pass to these functions.
- Always run tests for your changes before reporting back.

## CMake Regression Check Scripts
- `tests/cmake/scripts/CheckNoTimeout.cmake` validates a command completes within a timeout and can optionally enforce an exact exit code via `EXPECT_RC`.
- `tests/cmake/scripts/CheckTuHeaderCaseCollision.cmake` validates codegen fails when per-TU generated header names collide case-insensitively.
- `tests/cmake/scripts/CheckMockCrossRootInclude.cmake` validates mock codegen succeeds for cross-root/cross-drive header includes on Windows (falls back to absolute include paths when relative paths are impossible).
- In `tests/CMakeLists.txt`, prefer `gentest_add_cmake_script_test(...)` for these checks and pass required `DEFINES` explicitly (for example `TIMEOUT_SEC`, `EXPECT_RC`, `BUILD_ROOT`, `TARGET_ARG`).

## Commit & Pull Request Guidelines
- Commits: use Conventional Commits (`type(scope): subject`) with a short imperative subject, for example `feat(codegen): attach generated registration shims`; mark breaking changes with `!`; add context in the body when needed; use trailers like `Refs: #123`.
- PRs: describe scope, list exercised build presets (unit/integration and sanitizers), and include failing output snippets for regressions or flaky behavior.

## Tooling & Configuration Tips
- `gentest_attach_codegen()` turns on `EXPORT_COMPILE_COMMANDS` for its target with Ninja and Makefile generators, because codegen reads flags from `compile_commands.json`. This repository also sets `CMAKE_EXPORT_COMPILE_COMMANDS ON`, which `scripts/check_clang_tidy.sh` and `scripts/coverage_hygiene.py` rely on.
- vcpkg-based presets take their dependencies from `vcpkg.json` (baseline pinned in `vcpkg-configuration.json`); declare and pin every package dependency there.
- Modules:
  - See `docs/modules.md` for the supported named-module flows (`import gentest;`, `import gentest.mock;`, `import gentest.bench_util;`).
  - `import gentest;` consumers should link `gentest::gentest`; test executables typically link both `gentest::gentest` and `gentest::gentest_main` (or `gentest::gentest_runtime` if they provide their own `main()`).
  - Set `GENTEST_ENABLE_PUBLIC_MODULES` to `ON` (or `AUTO` for toolchain-driven fallback) when you test public modules.
  - Link mock targets with the public `gentest_link_mocks()`, before or after `gentest_attach_codegen()`, or with `target_link_libraries()` before `gentest_attach_codegen()`, so the codegen scan sees the generated mock headers. A `target_link_libraries()` call after `gentest_attach_codegen()` misses the codegen ordering and the `DEFS` include-path check.
- `gentest_attach_codegen()` without `MODULE_REGISTRATION` uses header registration. Use Ninja or Ninja Multi-Config.
- `OUTPUT_DIR` must be a concrete path without generator expressions in every mode, and unique per target; multi-config generators add their configuration subdirectory automatically.
- Cross-compiling (target = arm/riscv/etc, host runs codegen):
  - `GENTEST_BUILD_CODEGEN` defaults to `OFF` for cross builds. Native builds default it to `ON` when installing, otherwise to `gentest_BUILD_TESTING`. A native install with `-DGENTEST_BUILD_CODEGEN=OFF` installs only the runtime, and its consumers must select a host tool.
  - Build the host tool separately, then point the target build at it with `GENTEST_CODEGEN_EXECUTABLE`:
    - Host tool (native) build: `cmake --workflow --preset=host-codegen` (writes `build/host-codegen/tools/gentest_codegen`), or `cmake -S . -B build/host -Dgentest_BUILD_TESTING=OFF -DGENTEST_BUILD_CODEGEN=ON` followed by `cmake --build build/host --target gentest_codegen`.
    - Target build: `cmake -S <proj> -B build/target -DCMAKE_TOOLCHAIN_FILE=<toolchain.cmake> -DGENTEST_BUILD_CODEGEN=OFF -DGENTEST_CODEGEN_EXECUTABLE=<path-to-host-gentest_codegen>`
  - `find_package(gentest CONFIG REQUIRED)` includes `GentestCodegen.cmake`. Native consumers find `gentest_codegen` in the installed prefix; cross consumers must set a host `GENTEST_CODEGEN_EXECUTABLE` (or `GENTEST_CODEGEN_TARGET`) before `gentest_attach_codegen()`.
  - Consumer cross builds: [docs/buildsystems/host_toolchain_sysroots.md](docs/buildsystems/host_toolchain_sysroots.md). Testing this repository under QEMU: [docs/cross_compile/linux_qemu.md](docs/cross_compile/linux_qemu.md).
- Notes for Windows + system LLVM:
  - With Clang on Windows, `cmake/GentestDependencies.cmake` sets `FMT_USE_CONSTEVAL=0` (a Clang constant-evaluation issue) and, for a top-level build, the static release CRT (unless `CMAKE_MSVC_RUNTIME_LIBRARY` is set) and `_ITERATOR_DEBUG_LEVEL=0` to match prebuilt LLVM packages.
  - Some LLVM Windows packages reference a missing `diaguids.lib`; `gentest_patch_llvm_missing_diaguids(LLVMDebugInfoPDB)` (`cmake/GentestLlvmDependencyShims.cmake`, called from `tools/CMakeLists.txt`) substitutes the newest installed Visual Studio DIA SDK copy.
  - `-DGENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS=ON` disables, in the Debug configuration on Windows only, the tests in the `if(WIN32 AND GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS)` blocks of `tests/CMakeLists.txt` and `tests/cmake/Regressions.cmake`: the concurrency and async death tests, `regression_logging_output_unadopted_thread_log_death`, and the no-exceptions local-fixture teardown regressions. These hang in some Windows Debug environments. `runtime_recording` and `gentest_discover_tests_smoke` stay enabled but skip their aborting probes. CI never sets it: its Windows jobs build `RelWithDebInfo`, which `tests/scripts/test_manual_ci.py` checks.

## `gentest_codegen` parallelism (per-slot modes)
- Applies when one `gentest_codegen` invocation has several scan slots (`--tu-out-dir`), in header registration and for named-module inputs.
- A "codegen job" is a **worker thread inside a single `gentest_codegen` process** (not a CMake/Ninja job).
- Controls:
  - `gentest_codegen --jobs=<N>` (0 = auto: `std::thread::hardware_concurrency()`, at most one worker per slot).
  - The `GENTEST_CODEGEN_JOBS=<N>` environment variable (same values, plus `auto`) applies only when `--jobs` is absent; `--jobs` overrides it, including `--jobs=0`.
  - CMake builds always pass `--jobs=<N>` from the cache variable `GENTEST_CODEGEN_JOBS` (default `1`, or the environment variable's value at the first configure; `AUTO` means 0); reconfigure with `-DGENTEST_CODEGEN_JOBS=<N>` to change it. Scripts that set only the environment variable (`scripts/bench_compile.py --codegen-jobs`, `scripts/verify_codegen_parallel.py`) therefore run with the configured value.
- Flow of one invocation: worker threads scan the authored translation units first, then only the listed headers that no authored TU reached; a single thread merges the results (declaration sites, entities, fixtures) and picks one stable owner slot per entity; worker threads then write one `tu_XXXX_<stem>.header_registration.gentest.cpp` per slot (a slot that owns nothing gets a valid empty source); mock outputs are written last on one thread. Output is the same for any job count.
- Design notes, the full flow diagram and verification tooling live in `docs/stories/005_codegen_parallelism_jobs.md`, which is kept current with this section.
