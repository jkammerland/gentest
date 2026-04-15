# Story: Separate Core Reporting From Optional Allure And Format Sinks

## Goal

Split core runtime reporting from optional Allure-specific machinery and other
format sinks so the default runtime path is easier to read, test, and evolve.

This story is about concern separation, not changing user-visible reporting
formats.

## Problem

`src/runner_reporting.cpp` currently mixes several responsibilities in one
translation unit:

- core case/result reporting
- JUnit generation
- GitHub annotation output
- optional Allure staging and related conditional logic

That creates a few recurring problems:

- the default runtime path carries optional integration complexity
- `#ifdef`-heavy paths are harder to review and test
- output-format changes have a larger blast radius than necessary
- optional integration code is not clearly isolated from core reporting

## User stories

As a maintainer, I want core reporting behavior to live apart from optional
Allure support, so routine runtime work does not require reasoning about every
report sink at once.

As a contributor, I want reporting sinks to have clearer boundaries, so I can
change JUnit, GitHub annotations, or Allure behavior without reopening unrelated
formatting paths.

As a reviewer, I want optional integration code to be isolated enough that
default runtime changes stay small and obvious.

## Scope

In scope:

- `src/runner_reporting.cpp`
- Allure-specific staging and helper code
- separation of core reporting abstractions from optional sinks

Out of scope:

- changing report semantics intentionally
- redesigning JUnit or GitHub annotation contracts
- adding new reporting formats as part of this split

## Design direction

Split reporting by responsibility, not by arbitrary file size.

Preferred shape:

1. core reporting state and shared utilities
2. format-specific sinks:
   - console/core summary
   - JUnit
   - GitHub annotations
   - Allure
3. optional integrations compiled and linked through narrow interfaces rather
   than living inline with default runtime code

Keep shared formatting or escaping helpers centralized only when they are
genuinely shared across sinks.

## Rollout

1. Inventory reporting functions by sink and shared dependency.
2. Extract Allure-specific code first because it is the clearest optional
   integration boundary.
3. Split remaining sink-specific helpers only where it reduces real coupling.
4. Re-run output-sensitive reporting tests after each split, including:
   - `junit_smoke_unit`
   - `junit_properties_unit`
   - `outcomes_junit_runtime_skip_prefix_user_text`
   - `outcomes_junit_xfail_mapping`
   - `outcomes_junit_xpass_mapping`
   - `github_annotations_smoke`
   - `regression_runtime_selection_mixed_summary_counts`
   - `regression_runtime_selection_duplicate_name_summary_first_location`
   - `regression_runtime_selection_duplicate_name_summary_second_location`
   - `regression_logging_output_on_failure_policy_pass_silent`
   - `regression_logging_output_always_policy_visible_on_pass`
   - `regression_logging_output_default_always_policy_visible_on_pass`
   - `regression_logging_output_explicit_never_overrides_default_always`
   - `regression_runtime_reporting_fallback_assertion_junit_reports_failure`
   - `regression_runtime_reporting_github_annotation_escapes_file_title`
   - `regression_runtime_reporting_junit_cdata_token_split`
   - `regression_runtime_reporting_junit_open_failure_is_visible`
   - `regression_runtime_reporting_measured_case_lines_use___line__`
   - `regression_bench_only_junit_generation`
   - `regression_jitter_only_junit_generation`
   - `regression_bench_failure_junit_generation`
   - `regression_jitter_failure_junit_generation`
   - `gentest_outcome_matrix_stale_junit_guard`
5. When Allure tests are enabled, also re-run:
   - `allure_smoke_single`
   - `allure_logs_attachment`
   - `allure_logs_attachment_has_plain_text_type`
   - `allure_logs_attachment_file`
   - `allure_bench_metrics_attachment_json`
   - `allure_bench_metrics_attachment_file`
   - `allure_bench_plot_attachment_json`
   - `allure_bench_plot_attachment_file`
   - `allure_jitter_histogram_attachment_json`
   - `allure_jitter_histogram_attachment_file`
   - `allure_jitter_plot_attachment_json`
   - `allure_jitter_plot_attachment_file`
   - `allure_jitter_samples_attachment_json`
   - `allure_jitter_samples_attachment_file`
   - `allure_jitter_samples_attachment_schema`
   - `regression_allure_directory_failure_reports_infra`
   - `regression_allure_result_write_failure_reports_infra`
   - `regression_allure_result_write_failure_keeps_junit_in_sync`
   - `regression_junit_open_failure_keeps_allure_in_sync`
   - `regression_allure_runner_infra_failure_parity`
   - `regression_allure_attachment_name_collision`
6. On Unix hosts that provide `/dev/full`, also re-run:
   - `regression_junit_write_failure_keeps_allure_in_sync`

## Acceptance criteria

- Core reporting code no longer lives in the same large implementation unit as
  optional Allure staging.
- Optional Allure behavior is isolated behind a separate implementation unit or
  adapter boundary instead of living inline with the main core-reporting path.
- `junit_smoke_unit`, `junit_properties_unit`, `github_annotations_smoke`, and
  `regression_runtime_selection_mixed_summary_counts`,
  `regression_runtime_selection_duplicate_name_summary_first_location`,
  `regression_runtime_selection_duplicate_name_summary_second_location`,
  `regression_logging_output_on_failure_policy_pass_silent`,
  `regression_logging_output_always_policy_visible_on_pass`,
  `regression_logging_output_default_always_policy_visible_on_pass`,
  `regression_logging_output_explicit_never_overrides_default_always`,
  `regression_runtime_reporting_fallback_assertion_junit_reports_failure`,
  `regression_runtime_reporting_github_annotation_escapes_file_title`,
  `regression_runtime_reporting_junit_cdata_token_split`,
  `regression_runtime_reporting_junit_open_failure_is_visible`, and
  `regression_runtime_reporting_measured_case_lines_use___line__`
  still pass without intentional output changes.
- `outcomes_junit_runtime_skip_prefix_user_text`,
  `outcomes_junit_xfail_mapping`,
  `outcomes_junit_xpass_mapping`,
  `regression_bench_only_junit_generation`,
  `regression_jitter_only_junit_generation`,
  `regression_bench_failure_junit_generation`, and
  `regression_jitter_failure_junit_generation`, and
  `gentest_outcome_matrix_stale_junit_guard`
  still pass without intentional output changes.
- When `GENTEST_ENABLE_ALLURE_TESTS=ON`,
  `allure_smoke_single`,
  `allure_logs_attachment`,
  `allure_logs_attachment_has_plain_text_type`,
  `allure_logs_attachment_file`,
  `allure_bench_metrics_attachment_json`,
  `allure_bench_metrics_attachment_file`,
  `allure_bench_plot_attachment_json`,
  `allure_bench_plot_attachment_file`,
  `allure_jitter_histogram_attachment_json`,
  `allure_jitter_histogram_attachment_file`,
  `allure_jitter_plot_attachment_json`,
  `allure_jitter_plot_attachment_file`,
  `allure_jitter_samples_attachment_json`,
  `allure_jitter_samples_attachment_file`,
  `allure_jitter_samples_attachment_schema`,
  `regression_allure_directory_failure_reports_infra`,
  `regression_allure_result_write_failure_reports_infra`,
  `regression_allure_result_write_failure_keeps_junit_in_sync`,
  `regression_junit_open_failure_keeps_allure_in_sync`,
  `regression_allure_runner_infra_failure_parity`, and
  `regression_allure_attachment_name_collision`
  still pass.
- On Unix hosts that provide `/dev/full`,
  `regression_junit_write_failure_keeps_allure_in_sync`
  still passes.
- The reporting code structure makes sink ownership clearer to contributors and
  reviewers.
