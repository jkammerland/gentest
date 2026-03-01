# Design: `runner_impl` Modularization (Execution + Fixtures + Reporting)

## Status

This design starts from the current branch state:

1. Already extracted:
   - `runner_cli`
   - `runner_selector`
   - `runner_test_plan`
   - `runner_case_invoker`
   - `runner_result_model`
2. Still concentrated in `runner_impl.cpp`:
   - shared fixture runtime ownership/lifecycle
   - measured bench/jitter execution orchestration
   - report model accumulation + JUnit/Allure/summary emission
   - final exit policy wiring

This document defines the next extraction boundaries and interfaces.

## Problem Statement

`runner_impl.cpp` still mixes four responsibilities:

1. lifecycle/stateful runtime (`shared_fixture_registry`, setup/teardown semantics)
2. execution orchestration (test loop + measured loop + fail-fast gates)
3. reporting/accounting (summary counters, JUnit/Allure projection)
4. process-level policy (final exit code)

This makes correctness fixes expensive because one change can affect summary text, report artifacts, and exit code at once.

## Goals

1. Keep runtime behavior stable while reducing blast radius.
2. Move mutable runtime concerns behind explicit APIs (no hidden cross-module mutation).
3. Make one typed run outcome feed all reporters and exit policy.
4. Keep `run_all_tests` as a thin coordinator.

## Non-Goals

1. Rewriting benchmark statistics math.
2. Changing CLI contract or user-facing output format.
3. Replacing the existing fixture model (suite/global/local semantics stay as-is).

## Proposed Module Boundaries

1. `runner_fixture_runtime`
   - owns shared fixture registration state and run-lifecycle handles
   - exports typed setup/teardown diagnostics
2. `runner_measured_executor`
   - executes selected bench/jitter cases
   - emits typed measured verdicts + display rows
3. `runner_reporting`
   - consumes typed verdict stream
   - emits console summary + JUnit + Allure
4. `runner_entry` (implemented inside `runner_impl.cpp` initially)
   - orchestration only: parse -> select -> plan -> execute -> finalize -> report -> return

## Data Model Unification

Use one run accumulator as source of truth for:

1. case verdicts (test + bench + jitter)
2. infra diagnostics (fixture setup/teardown/registration)
3. summary projections
4. exit decision

### Example: Unified Outcome Model

```cpp
// src/runner_execution_model.h (proposed)
namespace gentest::runner {

enum class CaseKind { Test, Bench, Jitter };

struct CaseVerdict {
    std::string name;
    std::string suite;
    CaseKind    kind = CaseKind::Test;
    Outcome     outcome = Outcome::Pass; // pass/fail/skip/xfail/xpass
    double      time_s = 0.0;
    std::vector<std::string> failures;
    std::string skip_reason;
    std::vector<std::string> tags;
    std::vector<std::string> requirements;
};

struct InfraDiagnostic {
    std::string component; // e.g. "shared_fixture_setup"
    std::string message;
    bool        fail_run = true;
};

struct RunAccumulator {
    std::vector<CaseVerdict>    verdicts;
    std::vector<InfraDiagnostic> infra;
};

} // namespace gentest::runner
```

## Fixture Runtime API

### Example: Explicit Fixture Runtime Handle

```cpp
// src/runner_fixture_runtime.h (proposed)
namespace gentest::runner {

struct FixtureSetupResult {
    bool ok = true;
    std::vector<std::string> errors;
};

struct FixtureTeardownResult {
    bool ok = true;
    std::vector<std::string> errors;
};

class SharedFixtureRunHandle {
public:
    SharedFixtureRunHandle();
    FixtureSetupResult setup() const;
    FixtureTeardownResult teardown(); // idempotent
private:
    bool finalized_ = false;
};

} // namespace gentest::runner
```

Key rule:

1. `get_shared_fixture(...)` remains lookup-only for runtime users.
2. run-level `setup()` and `teardown()` are explicit coordinator steps.
3. setup/registration failures are returned as typed diagnostics, not inferred from side effects.

## Measured Executor API

### Example: Bench/Jitter Executor Contract

```cpp
// src/runner_measured_executor.h (proposed)
namespace gentest::runner {

struct MeasuredExecutionResult {
    std::vector<CaseVerdict> verdicts;
    bool stopped = false; // fail-fast stop
};

MeasuredExecutionResult run_selected_benches(std::span<const gentest::Case> cases,
                                             std::span<const std::size_t> idxs,
                                             const CliOptions& opt,
                                             bool fail_fast);

MeasuredExecutionResult run_selected_jitters(std::span<const gentest::Case> cases,
                                             std::span<const std::size_t> idxs,
                                             const CliOptions& opt,
                                             bool fail_fast);

} // namespace gentest::runner
```

Rules:

1. executor owns measured path error classification
2. coordinator only merges results into `RunAccumulator`
3. reporting never re-derives semantics from raw exceptions

## Reporting API

### Example: Reporter Projection Surface

```cpp
// src/runner_reporting.h (proposed)
namespace gentest::runner {

struct ReportConfig {
    const char* junit_path = nullptr;
    const char* allure_dir = nullptr;
    bool color_output = true;
    bool github_annotations = false;
};

struct SummaryProjection {
    std::size_t passed = 0;
    std::size_t failed = 0;
    std::size_t skipped = 0;
    std::size_t xfail = 0;
    std::size_t xpass = 0;
};

SummaryProjection compute_summary(const RunAccumulator& acc);
void write_reports(const RunAccumulator& acc, const ReportConfig& cfg);
void print_console_summary(const RunAccumulator& acc, const ReportConfig& cfg);
int  compute_exit_code(const RunAccumulator& acc);

} // namespace gentest::runner
```

Rule: `compute_exit_code()` and `compute_summary()` must share the same failure classification.

## Coordinator Flow

### Example: Target Shape for `run_all_tests`

```cpp
int run_all_tests(std::span<const char*> args) {
    CliOptions opt{};
    if (!parse_cli(args, opt)) return 2;

    auto cases = get_case_span();
    auto sel = select_cases(cases, opt);
    if (auto ec = handle_selection_status(sel, opt, cases); ec >= 0) return ec;

    RunAccumulator acc{};
    SharedFixtureRunHandle fx{};

    append_fixture_setup_diagnostics(acc, fx.setup());
    run_selected_tests_into(acc, cases, sel.test_idxs, opt);
    run_selected_benches_into(acc, cases, sel.bench_idxs, opt);
    run_selected_jitters_into(acc, cases, sel.jitter_idxs, opt);
    append_fixture_teardown_diagnostics(acc, fx.teardown());

    ReportConfig rcfg{.junit_path = opt.junit_path, .allure_dir = opt.allure_dir,
                      .color_output = opt.color_output, .github_annotations = opt.github_annotations};
    write_reports(acc, rcfg);
    print_console_summary(acc, rcfg);
    return compute_exit_code(acc);
}
```

## Migration Plan (Commit-Scoped)

1. Extract `runner_fixture_runtime` without semantic changes.
2. Extract `runner_reporting` (keep current text/artifact output stable).
3. Extract `runner_measured_executor` (bench/jitter orchestration only).
4. Introduce `RunAccumulator` and migrate coordinator to use it as single source of truth.
5. Shrink `runner_impl.cpp` to wiring and final mode dispatch.

## Regression Gates Required Before/During Extraction

1. Keep all current regressions green (`ctest --preset=debug-system`).
2. Add/retain invariants for:
   - measured call-phase fail paths
   - measured non-infra setup skip behavior
   - shared fixture setup/teardown infra reporting parity (summary + JUnit + exit)
   - CLI ambiguity/kind mismatch diagnostics
3. Add focused unit tests for `runner_selector` and `runner_test_plan` pure logic modules.

## Risks and Mitigations

1. Risk: exit code / summary / JUnit drift.
   - Mitigation: compute all three from the same `RunAccumulator`.
2. Risk: fixture lifecycle regressions after extraction.
   - Mitigation: explicit `setup()`/`teardown()` handle API + existing fixture regressions.
3. Risk: bench/jitter behavior drift while separating display/report logic.
   - Mitigation: executor returns typed verdicts; table rendering remains read-only projection.

## Review Questions

1. Do you want `runner_reporting` to fully own GitHub annotation output, or keep annotations in executor paths?
2. Should we enforce `get_shared_fixture()` as strict lookup-only now, or after fixture runtime extraction lands?
3. Do you want `RunAccumulator` introduced in one commit (larger diff) or split into adapter commits (safer review)?

## Reviewed Decisions (Accepted)

1. `runner_reporting` fully owns GitHub annotation emission.
2. `get_shared_fixture()` is strict lookup-only now (no lazy setup side effects).
3. `RunAccumulator` transition lands as one commit.
