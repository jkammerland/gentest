# FuzzTest + Centipede: long read (for gentest integration)

> This document is written as a **durable reference for future AI agents and maintainers**.
> It summarizes how Google **FuzzTest** works, what **Centipede** is in relation to it, and what
> integration constraints matter for `gentest`.
>
> **Upstream snapshot:** `google/fuzztest` @ `a72f099a943c257afe8d4d87c10a22b23e17786d` (2026-01-09),
> cloned on 2026-01-11.

## Executive summary

- **FuzzTest** is a C++ framework for writing *fuzz tests* (property-based tests) and running them using
  coverage-guided fuzzing.
- A fuzz test is primarily a **property function**: `void Property(T1, T2, ...)` that asserts invariants.
- The normal authoring API is macro-based: `FUZZ_TEST(SuiteName, Property)` plus optional `.WithDomains(...)`
  and `.WithSeeds(...)`.
- There is also an **advanced, macro-free registration API**:
  - `fuzztest::GetRegistration(...)` / `fuzztest::GetRegistrationWithFixture(...)`
  - `fuzztest::RegisterFuzzTest(...)`
  This is very relevant for `gentest_codegen` because it avoids token-pasting constraints.
- Build/run modes (CMake quickstart terminology):
  - **Unit test mode** (default): no coverage instrumentation; `FUZZ_TEST`s run quickly with random inputs as
    part of a normal `gtest` run.
  - **Fuzzing mode** (`-DFUZZTEST_FUZZING_MODE=ON`): target code is built with sanitizer coverage +
    comparison tracing; a single fuzz test typically runs continuously (`--fuzz=Suite.Test`) until crash/stop
    (or until `--fuzz_for=...` time budget).
  - **Compatibility mode** (`-DFUZZTEST_COMPATIBILITY_MODE=libfuzzer`, experimental): uses an external engine.
- **Centipede** is a separate fuzzing engine that lives in the same repo. FuzzTest can be compiled to use it
  (via `FUZZTEST_USE_CENTIPEDE` in upstream Bazel builds), but the CMake quickstart does not appear to enable
  it by default.

## What “FuzzTest” is (conceptually)

### Property function

The core unit is a property function:

- Signature is typically `void f(T1, T2, ...)` (return value is ignored; the goal is to trigger:
  - explicit assertion failures (e.g., `EXPECT_*`, `ASSERT_*`), and/or
  - sanitizer/UB crashes.
- Best practices (high impact for effectiveness):
  - deterministic execution for the same inputs,
  - avoid depending on global state,
  - join threads before returning,
  - keep tests small and focused (many small fuzz tests beat one huge one).

### Domains

FuzzTest generates values for each parameter from an **input domain**:

- Default domain per parameter type `T` is `fuzztest::Arbitrary<T>()`.
- You can override domains with `.WithDomains(...)`, e.g. `Positive<int>()`, `InRange(...)`, `InRegexp(...)`, etc.
- Domains are central to “smart” fuzzing: you can constrain inputs to reach deeper parsing/semantic logic
  instead of being rejected at the boundary.

See upstream:
- `doc/domains-reference.md`
- `doc/fuzz-test-macro.md` (domains + seeds)

### Seeds (and why they matter)

Seeds are concrete values used to bootstrap exploration:

- `.WithSeeds({{...}, {...}})` provides initial examples; the engine mutates around them.
- Upstream calls out a common pitfall: `FUZZ_TEST` expands to a **global variable**, so initializing seeds from
  globals in other translation units can cause static initialization order problems.
- Workarounds include:
  - using a **seed provider** (callable returning `std::vector<std::tuple<Args...>>`), or
  - reading seed files from a directory (`ReadFilesFromDirectory`).

See upstream: `doc/fuzz-test-macro.md` (“Initial seeds” + “seed providers”).

## Authoring surface: macros vs advanced API (important for gentest)

### Standard macros

The two primary macros are:

- `FUZZ_TEST(SuiteName, PropertyFunction)` for free functions
- `FUZZ_TEST_F(FixtureType, PropertyMethod)` for fixture-based fuzz tests

Macros chain configuration methods like:

- `.WithDomains(...)`
- `.WithSeeds(...)`

See upstream: `doc/fuzz-test-macro.md`, `doc/fixtures.md`, `fuzztest/fuzztest_macros.h`.

### Macro constraints you must account for

The implementation uses **token pasting** to construct a unique registration symbol:

- Upstream: `fuzztest/internal/registry.h` defines `INTERNAL_FUZZ_TEST(suite_name, func)` and creates a symbol
  like `fuzztest_reg___<suite>___<func>`.
- Because of token-pasting, the macro parameters must be tokens that can appear inside an identifier:
  - `suite_name` cannot contain `/`, `-`, `::`, etc.
  - `func` cannot be a qualified name like `ns::Property` (also breaks token pasting).

This is a major reason why a `gentest` integration should likely avoid emitting `FUZZ_TEST(...)` directly.

### Advanced API (best fit for codegen)

FuzzTest exposes a macro-free registration API:

- `fuzztest::GetRegistration(std::string suite, std::string test, std::string file, int line, TargetFunction fn)`
- `fuzztest::GetRegistrationWithFixture<Fixture>(...)`
- `fuzztest::RegisterFuzzTest(reg)`

These allow:
- passing string suite/test names (no identifier restrictions),
- passing a callable/function pointer without needing an unqualified token name,
- using stable names that can match `gentest`’s naming (`suite/path/name`) if desired.

**Practical pattern for generated code**

Even without a custom `main()`, you can register at static init time with a small lambda:

```cpp
[[maybe_unused]] static int gentest_fuzz_reg_0 = [] {
    fuzztest::RegisterFuzzTest(
        fuzztest::GetRegistration("suite", "name", __FILE__, __LINE__, +TargetFn)
            .WithDomains(/*...*/)
            .WithSeeds(/*...*/));
    return 0;
}();
```

This keeps the “drop-in gtest main” workflow (`fuzztest_gtest_main.cc`) viable while avoiding the macro.

Upstream references:
- `fuzztest/internal/registration.h` (`GetRegistration*`)
- `fuzztest/internal/registry.h` (`RegisterFuzzTest`)
- `fuzztest/fuzztest_gtest_main.cc` (calls `fuzztest::InitFuzzTest(...)`)

## Build modes & flags (CMake view)

### Unit test mode (default)

- No sanitizer coverage flags required.
- `FUZZ_TEST` cases run quickly as part of normal unit test execution.
- Good for:
  - presubmit smoke,
  - catching obvious regressions with “some randomness”.

### Fuzzing mode

Enabled by configuring FuzzTest’s CMake with:

- `-DFUZZTEST_FUZZING_MODE=ON`

The quickstart recommends compiling the **target project** (not the FuzzTest framework itself) with:

- `-g -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -UNDEBUG`
- `-fsanitize-coverage=inline-8bit-counters -fsanitize-coverage=trace-cmp`
- commonly also `-fsanitize=address -DADDRESS_SANITIZER` (or other sanitizers)

Upstream provides a convenience macro that applies these flags globally:

- `fuzztest_setup_fuzzing_flags()` in `cmake/FuzzTestFlagSetup.cmake`

**Integration note for gentest:**
`gentest` should prefer *target-scoped* flags rather than global `CMAKE_CXX_FLAGS` mutation (so fuzzing flags
don’t leak into non-fuzz targets).

### Compatibility mode (libFuzzer, experimental)

Enabled in upstream CMake with:

- `-DFUZZTEST_COMPATIBILITY_MODE=libfuzzer`

This is explicitly marked experimental in `doc/quickstart-cmake.md`.

## Runtime flags & workflow (what users actually do)

FuzzTest uses a gtest binary with extra flags (implemented via Abseil flags).
Important flags are defined in `fuzztest/init_fuzztest.cc`.

Common “day-to-day” flows:

### List fuzz tests

- `--list_fuzz_tests`

### Continuous fuzzing of one test

- `--fuzz=SuiteName.TestName`

The quickstart shows fuzzing progress output including corpus size and edges covered.

### Time-bounded fuzzing (CI-friendly)

- `--fuzz_for=<duration>` (fuzz a set of fuzz tests for a time budget)
- `--time_budget_type=per_test|total` (see `fuzztest/internal/configuration.h`)

### Corpus database (persistent artifacts)

- Default: `--corpus_database=~/.cache/fuzztest`
- The `corpus_database` layout is described in the flag help text in `fuzztest/init_fuzztest.cc`:
  per-binary subdir, then per-test subdir, with `regression`/`coverage`/`crashing` inputs.

**Integration note for gentest:**
For reproducible CI behavior, it’s usually better to set `--corpus_database` to a workspace-local directory so
the job artifacts are captured and not written into `$HOME`.

### Replay corpus

- `--replay_corpus=SuiteName.TestName`
- `--replay_corpus_for=<duration>`

### Reproduce crashes as separate gtests

- `--reproduce_findings_as_separate_tests`

Upstream registers additional tests suffixed with `/Regression/<bug_id>` when enabled
(`fuzztest/internal/googletest_adaptor.cc`).

## Where Centipede fits

### Centipede (as a standalone engine)

The repo contains `centipede/`, which documents Centipede as a distributed fuzzing engine, with concepts like:

- feature-based coverage signal,
- shards, workdirs, corpus distillation/merging,
- out-of-process execution model (target crashes don’t kill the fuzzer).

See upstream: `centipede/README.md`.

### Centipede (as a FuzzTest backend)

FuzzTest’s internal registry selects a fuzzer implementation based on compile-time defines:

- `FUZZTEST_COMPATIBILITY_MODE` → external engine adaptor
- `FUZZTEST_USE_CENTIPEDE` → `CentipedeFuzzerAdaptor`
- otherwise → `FuzzTestFuzzerImpl` (FuzzTest’s built-in engine)

See upstream: `fuzztest/internal/registry.h`.

**Important nuance (for planning):**
In the upstream snapshot, `FUZZTEST_USE_CENTIPEDE` appears to be primarily wired via Bazel, not the CMake
quickstart, so “FuzzTest (CMake) == Centipede” is not automatically true.

## Practical integration guidance for gentest (future work)

### Why `gentest_codegen` is a strong match

FuzzTest is macro-first, but it provides just enough programmatic hooks to support code generation:

- A clang-tooling generator can:
  - discover `[[using gentest: fuzz(...)]]`,
  - validate function signatures,
  - generate a TU that calls `RegisterFuzzTest(GetRegistration(...))` with:
    - stable naming
    - `.WithDomains(...)` / `.WithSeeds(...)` derived from attributes.

This leverages `gentest`’s strengths (attribute discovery and codegen) while still using a mature fuzzing runtime.

### Key design choices to lock down early

1. **Naming model**
   - Do we map `gentest` names (`suite/path/name`) to:
     - gtest-style `Suite.Test` (more ergonomic with existing flags), or
     - keep slashes in test names (may work, but should be validated against gtest filters)?

2. **Binary layout**
   - one fuzz executable per suite/target (parallel-friendly), or
   - one “all fuzz targets” executable with selection flags.

3. **Mode separation**
   - default CI: unit-test mode fuzz tests (fast),
   - optional CI job: fuzzing mode (time-bounded, uses sanitizer coverage flags),
   - optional local/dev: continuous fuzzing.

4. **Centipede explicitly**
   - If we truly want Centipede:
     - do we build Centipede as a tool (Bazel or custom CMake), or
     - do we accept FuzzTest’s built-in engine for CMake-first integration?

### “If you are an AI agent updating this doc”

Recommended upstream files to re-check (most likely to change):

- `doc/quickstart-cmake.md` (modes, flags, run commands)
- `fuzztest/init_fuzztest.cc` (flag surface)
- `fuzztest/internal/registry.h` (engine selection + registration)
- `fuzztest/internal/registration.h` (advanced API contract)
- `fuzztest/internal/googletest_adaptor.cc` (how names become gtests, regression tests)
- `cmake/FuzzTestFlagSetup.cmake` (exact flag recipes)
- `centipede/README.md` (Centipede philosophy + workflow)

