# Story: modularize `runner_impl` and harden pre-refactor regression coverage

## Goal

Capture a concrete modularization plan for `src/runner_impl.cpp` based on 5 independent reviews, then define the regression tests we should add before large refactors.

## Independent review synthesis (5 subagents)

### Most frequent problem clusters

1. **Cross-cutting god-file architecture** (`4/5`)
   - `runner_impl.cpp` currently mixes CLI parsing, case selection, fixture lifecycle, execution, reporting, and exit policy.
   - High change blast radius and difficult reasoning across paths.

2. **Outcome/accounting/reporting fragmentation** (`4/5`)
   - Multiple status channels (`Counters`, `TimedRunStatus`, `Outcome`, report item fields, fixture guard status).
   - Risk of summary/report/exit divergence.

3. **Duplicated execution semantics across paths** (`4/5`)
   - Similar context + exception + skip/fail handling exists in test path, measured setup/teardown path, and measured call loops.
   - High drift risk after behavior changes.

4. **Shared fixture lifecycle coupling/global state complexity** (`3/5`)
   - Global mutable registries and lazy setup behavior in shared fixture lookup increase re-entrancy/order sensitivity and test complexity.

5. **Bench/jitter engine fragility and duplication** (`3/5`)
   - Duplication between bench and jitter pipelines.
   - One review flagged unguarded non-assert exception handling in call-phase loops as a potential correctness hole.

6. **CLI parsing and selection monoliths** (`3/5`)
   - Large parser and selection logic tightly coupled to orchestration and output behavior.

7. **Stringly diagnostics and regression brittleness** (`2/5`)
   - Runtime/test contracts often rely on exact message text.

## Core refactor principles

1. Keep behavior stable while modularizing: no semantic changes in extraction commits unless explicitly targeted.
2. Establish one typed source of truth for case/run outcomes before splitting execution/reporting.
3. Separate data/decision layers from presentation (console/JUnit/Allure).
4. Remove side effects from lookup-style APIs (notably shared fixture lookup).
5. Make phase behavior explicit with typed states instead of ad-hoc bool/string combinations.

## Target module boundaries

1. **`runner_cli`**
   - Parse/validate CLI options and produce structured diagnostics.
   - No direct stderr printing in parser.

2. **`runner_selector`**
   - Selection query evaluation: exact/suffix/filter/kind/death inclusion.
   - Deterministic `SelectionResult` + typed failure reason.

3. **`runner_fixture_runtime`**
   - Shared fixture registration + lifecycle orchestration.
   - Clear split between read lookup and setup/teardown mutation.

4. **`runner_case_invoker`**
   - Unified context + exception mapping for test/setup/call/teardown phases.

5. **`runner_test_executor`**
   - Test-case execution/grouping/fail-fast semantics.

6. **`runner_measurement_engine`**
   - Bench/jitter measurement loops and measurement-specific error handling.

7. **`runner_result_model`**
   - Typed `CaseVerdict` and `RunStatus`/`RunAccumulator` used by all paths.

8. **`runner_reporting`**
   - Console reporter + export reporters (JUnit/Allure) consuming typed verdicts.

9. **`runner_entry`**
   - Thin coordinator (`parse -> select -> plan -> execute -> finalize -> report -> exit`).

## Phased implementation plan

### Phase 0: lock behavior with characterization tests

Add missing regressions in this story before major extraction (see next section).

### Phase 1: introduce typed outcome model (minimal behavior change)

1. Add `CaseVerdict`/`RunStatus`/`RunAccumulator`.
2. Keep existing code paths, but adapt writes through the accumulator model.
3. Preserve output text/exit semantics.

### Phase 2: extract pure-ish orchestration pieces

1. Extract CLI parser.
2. Extract case selector.
3. Extract grouping/planning logic.

### Phase 3: extract reporters

1. Move summary/JUnit/Allure + annotation emission behind reporter interfaces.
2. Ensure reporters consume typed verdicts directly.

### Phase 4: unify invocation/execution semantics

1. Introduce `case_invoker` and move repeated context/exception logic there.
2. Move test and measured executors to dedicated modules.

### Phase 5: isolate fixture runtime

1. Extract shared fixture registry/lifecycle.
2. Eliminate implicit lifecycle mutation from lookup path.
3. Keep run-level setup/teardown explicit in entry coordinator.

### Phase 6: reduce `runner_impl.cpp` to coordinator

1. Keep only wiring and top-level control.
2. Delete or inline obsolete helpers in extracted modules.

## Pre-refactor regression hardening (recommended additions)

### A. Measured call-phase exception semantics

Add regressions proving bench/jitter call phase behavior for:

1. `std::exception` throw in call phase.
2. `gentest::failure` in call phase.
3. `gentest::skip` in call phase.

Expected contract should be documented and pinned by tests (currently under-specified by existing suite).

### B. Shared teardown reporting parity

Add regressions that run with `--junit`/`--allure-dir` and assert shared fixture teardown failures are represented in report artifacts consistently with exit code.

### C. Bench/jitter-only reporting

Add regression to validate report generation behavior for bench/jitter-only selections when `--junit`/`--allure-dir` is requested.

### D. Fixture-group shuffle semantics

Add a deterministic regression for fixture-group ordering/shuffle invariants (beyond seed-print smoke checks), especially around grouped member fixtures.

### E. Outcome/report mapping invariants

Add targeted checks that each logical outcome (`pass`, `fail`, `skip`, `xfail`, `xpass`, infra-skip) maps consistently to:

1. console summary counters
2. JUnit status/failure counts
3. Allure status
4. final exit code

## What to do before refactor starts

1. Implement A/B first (highest correctness risk).
2. Implement C/E next (prevents summary/report drift during extraction).
3. Implement D as guardrail before moving grouping/planner code.

## Current coverage vs gaps (as of this story)

### Already covered

1. Measured assertion propagation is covered:
   - `regression_bench_assert_failure_propagates`
   - `regression_jitter_assert_failure_propagates`
2. Measured call-phase non-assert propagation is covered:
   - `regression_bench_std_exception_failure_propagates`
   - `regression_bench_fail_failure_propagates`
   - `regression_bench_skip_failure_propagates`
   - jitter equivalents
3. Shared fixture teardown non-zero exit and report visibility are covered:
   - `regression_shared_fixture_teardown_failure_exits_nonzero`
   - `regression_shared_fixture_teardown_failure_junit_reports_failure`
   - `regression_shared_fixture_teardown_failure_junit_preserves_case_count`
   - `regression_shared_fixture_teardown_failure_junit_reports_detail`
   - `regression_shared_fixture_teardown_failure_summary_failed_count`
4. Infra skip accounting for shared fixture setup skips is covered:
   - free/member + JUnit failure-count checks
   - measured shared fixture setup skip checks

### Still missing before refactor

1. **Bench/jitter-only report generation contract**
   - No explicit check that `--kind=bench` / `--kind=jitter` with `--junit`/`--allure-dir` produces expected artifacts and status counts.
2. **Fixture-group shuffle invariants**
   - Existing shuffle check validates seed printing only, not fixture group ordering semantics.
3. **Cross-surface outcome consistency matrix**
   - Need one matrix test validating summary/JUnit/Allure/exit coherence for:
     - pass/fail/skip/xfail/xpass
     - infra-skip in test path
     - infra-skip in measured path
4. **CLI/selection characterization before extraction**
   - Add explicit coverage for ambiguous suffix matches, removed options, and kind mismatch diagnostics.
5. **Allure parity gate**
   - Decide whether infra-failure parity must be validated under `GENTEST_USE_BOOST_JSON` before extraction.

## Proposed pre-refactor regression backlog

### P0 (add before extraction starts)

1. `regression_bench_only_junit_generation`
2. `regression_jitter_only_junit_generation`
3. `regression_cli_kind_mismatch_diagnostics`
4. `regression_cli_suffix_ambiguity_diagnostics`

### P1 (add early during extraction)

1. `regression_fixture_group_shuffle_invariants`
2. outcome-consistency matrix script test (`summary + junit + exit`)
3. Allure parity checks for runner-level infra failures
