# Native Windows Inventory For Story 022

## Purpose

This file is the authoritative checked-in inventory artifact for
`022_windows_ci_portability_followups.md`.

Refresh it from a fresh native Windows:

`ctest --preset=debug-system --output-on-failure`

run before creating or updating non-path Windows follow-up stories.

## Inventory

Pending refresh.

Replace this section with one entry per still-failing native Windows test using
this shape:

- `<ctest-name>`: `<initial root-cause class>` -> `<destination story or close>`

Example classes:

- helper/path-depth -> `028_windows_helper_path_depth_portability.md`
- downstream ABI/linkage -> `<new child story>`
- public-module contract -> `<new child story>`
- already fixed -> `close`

## Notes

- Remove already-green tests from the inventory instead of carrying them
  forward as stale backlog.
- If every surviving failure maps cleanly to an existing story, close
  `022_windows_ci_portability_followups.md` once the split is done.
