# Story: Simplify Test Helper Infrastructure And Derive Inventory Expectations

## Goal

Reduce the test-helper layer to a smaller set of reusable drivers and eliminate
manual duplication in inventory/count expectations where the source of truth can
be derived mechanically.

This is a maintainability story. Windows path-depth and short-root policy are
tracked separately in `028_windows_helper_path_depth_portability.md`.

## Problem

The current test harness has too many thin wrappers around generic helper
behavior, and inventory expectations are maintained in multiple places:

- many helper scripts differ only in a few command arguments
- `GentestTests.cmake` and `tests/CMakeLists.txt` carry overlapping helper
  concepts
- hard-coded count expectations coexist with expected lists that could act as
  the source of truth
- helper-specific conventions increase review and maintenance noise

That costs time in several ways:

- small harness changes fan out across multiple scripts
- list/count updates are easy to miss
- reviewers have to verify duplicated expectations by hand

## User stories

As a maintainer, I want a smaller helper framework with one obvious way to run a
fixture-style contract check, so harness changes are local and auditable.

As a contributor, I want inventory expectations to be derived from actual test
listing or one canonical expected artifact, so I do not have to update counts in
multiple places for one new test.

As a reviewer, I want helper and inventory changes to show real contract
movement rather than repeated bookkeeping edits.

## Scope

In scope:

- `cmake/GentestTests.cmake`
- `tests/CMakeLists.txt`
- generic helper scripts under `tests/cmake/scripts/`
- manual count/list duplication where one source of truth can be used

Out of scope:

- Windows helper-root and path-depth policy, which belongs to story `028`
- replacing CTest
- removing targeted helper scripts that genuinely encode distinct contracts
- unrelated codegen/runtime refactors

## Design direction

Collapse the helper layer around a small number of parameterized drivers.

Preferred outcomes:

1. one generic helper path for:
   - configure/build fixture checks
   - run-and-assert checks
   - output/contract inspection

2. derive expectations where possible from:
   - `--list`
   - canonical expected-list files
   - generated inventories checked into one place

This story may simplify helper call shapes, but it must not redefine helper
work-dir or path-shortening policy. Story `028` owns that behavior.

The current inventory-owner table is `_gentest_suite_inventory_checks` in
`tests/CMakeLists.txt`; if the design changes, it should still leave one
declared source of truth for every inventory-style assertion.

## Rollout

1. Inventory helper scripts by behavior rather than filename.
2. Merge scripts that differ only by a few command parameters.
3. Pick one canonical source of truth for each inventory-style assertion.
4. Remove redundant hard-coded counts where they can be derived.
5. Re-run the helper-heavy inventory slice after each consolidation step,
   including:
   - `unit_inventory`
   - `integration_inventory`
   - `skiponly_inventory`
   - `fixtures_inventory`
   - `readme_fixtures_inventory`
   - `templates_inventory`
   - `mocking_inventory`
   - `helper_check_file_contains_rejects_stale_output`
   - `helper_allure_runner_infra_parity_rejects_stale_output`
   - `gentest_codegen_output_collision`
   - `gentest_discover_tests_smoke`
   - `gentest_tu_wrapper_source_props`

## Acceptance criteria

- Generic helper behavior in `GentestTests.cmake` and `tests/cmake/scripts/`
  is expressed through a documented smaller set of parameterized driver paths,
  not just one isolated consolidation.
- Every current inventory slice in `_gentest_suite_inventory_checks`
  (`unit`, `integration`, `skiponly`, `fixtures`, `readme_fixtures`,
  `templates`, and `mocking`) has one declared source of truth and no
  redundant manual count-update path remains for that slice.
- Adding or renaming tests in those inventory slices no longer requires
  synchronized updates to multiple redundant count locations.
- `unit_inventory`, `integration_inventory`, `skiponly_inventory`,
  `fixtures_inventory`, `readme_fixtures_inventory`,
  `templates_inventory`, `mocking_inventory`,
  `helper_check_file_contains_rejects_stale_output`, and
  `helper_allure_runner_infra_parity_rejects_stale_output`,
  `gentest_codegen_output_collision`,
  `gentest_discover_tests_smoke`, and
  `gentest_tu_wrapper_source_props`
  still pass after the consolidation.
