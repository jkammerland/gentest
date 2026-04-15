# Story: Refresh Native Windows Failure Inventory And Split Concrete Follow-Ups

## Goal

Produce a fresh checked-in native Windows failure inventory after the earlier
launcher, filename, object-path, and short-root fixes, then split any remaining
failures into concrete follow-up stories by mechanism.

This is an inventory-and-triage story, not a catch-all implementation bucket.

## Status

Done. The refreshed full native Windows inventory was captured on `2026-04-14`
from a normal deep checkout root, and every surviving failure was mapped to a
concrete owner story:

- mapped into a concrete mechanism-specific story
- already owned by `028_windows_helper_path_depth_portability.md`
- confirmed green and removed from the backlog

Several issues from older Windows failure inventories are already addressed and
are not open work here anymore:

- coverage-report contract checks already use the discovered Python interpreter
- the escaped-path depfile regression already avoids Windows-invalid `:` names
- some public-module and Xmake harnesses already apply Windows object-path or
  short-root mitigations

Path-depth and short-root policy now have a dedicated owner in
`028_windows_helper_path_depth_portability.md` and should not be duplicated
here.

## Problem

Native Windows still needs a current, honest failure inventory after those
earlier mitigations. The remaining work should be driven by a fresh native
Windows run, not by stale assumptions from an older red matrix.

Without that inventory, backlog ownership stays fuzzy:

- path-depth issues can be confused with non-path portability bugs
- already-fixed failures can linger as fake backlog
- unrelated Windows mechanisms can get lumped into one umbrella

## Current native Windows inventory

The authoritative checked-in inventory for this story lives in
`022_windows_ci_portability_followups_inventory.md`.

The latest inventory is the full native Windows
`ctest --preset=debug-system --output-on-failure` run from `2026-04-14`,
including the exact still-failing tests, their first-pass root-cause
classification, and destination story.

## User stories

As a maintainer, I want the native Windows backlog to start from a fresh checked-in
failure inventory, so every follow-up story begins from current evidence.

As a contributor, I want each remaining Windows failure mapped to one explicit
mechanism-specific owner, so I do not chase stale or mixed-cause failures.

As a reviewer, I want the inventory refresh and the subsequent product fixes to
be separated, so backlog clarity is established before implementation begins.

## Scope

In scope:

- re-baselining the current native Windows failure inventory
- recording the current failing tests and first-pass root-cause classes
- moving path-depth failures into story `028`
- creating or updating concrete follow-up stories for any remaining non-path
  failures
- closing this story if no surviving non-path Windows failures remain

Out of scope:

- implementing large product fixes directly in this story
- helper-root and short-path policy, which belongs to story `028`
- adding new Windows coverage work
- widening the Windows test matrix
- unrelated Linux/macOS cleanup

## Design direction

Inventory first, then split by mechanism.

Preferred ownership split:

1. helper-root and path-depth issues go to story `028`
2. already-green items leave the backlog immediately
3. any surviving non-path failure gets a concrete owner story instead of
   remaining here
4. this story closes once that split is complete

## Rollout

1. Run a fresh native Windows `ctest --preset=debug-system --output-on-failure`
   pass.
2. Update the checked-in `Current native Windows inventory` section or sibling
   artifact with the exact still-failing tests and their first-pass
   classification.
3. Remove any already-green item from this story.
4. Move helper/path-depth failures into story `028`.
5. Create or update one concrete follow-up story per surviving non-path
   mechanism.
6. Close this story once every surviving failure has a concrete destination.

## Acceptance criteria

- a checked-in refreshed native Windows failure inventory exists and names the
  exact failing tests from the latest native run
- no already-fixed launcher, filename, or helper-path issue remains listed as
  open work here
- path-depth and short-root failures are tracked only in story `028`
- every failing test in the refreshed inventory has an explicit root-cause
  class and a concrete destination story or closure decision
- this story is closed once the refreshed inventory has been split into
  concrete owned follow-up work
