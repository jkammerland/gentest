# Public API Surface Inventory For Story 023

## Purpose

This file is the authoritative classification artifact for
`023_public_api_internal_surface_reduction.md`.

Use it to record which installed symbols or surfaces are:

- supported public API
- unstable detail API
- private implementation machinery that should not be installed

## Inventory

Pending inventory.

Populate this file with the installed surface currently reachable from:

- `include/gentest/registry.h`
- `include/gentest/context.h`
- `include/gentest/fixture.h`

Suggested entry shape:

- `<symbol or surface>`: `<public | detail | private>` -> `<planned action>`

Example actions:

- keep
- move to `detail`
- hide behind opaque handle
- move to private header
- delete from installed surface
