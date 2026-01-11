# Story: fuzzing codegen backend abstraction (engine-pluggable)

## Goal

Refactor/extend `gentest_codegen` so fuzzing support is implemented as a **backend**:

- `gentest` has a stable attribute-level authoring model,
- codegen discovers fuzz targets into an engine-neutral model,
- emission is delegated to a selected backend (FuzzTest now; potentially `gentest` engine later).

## Motivation / user impact

- Allows switching fuzz engines without changing user code or public headers.
- Keeps engine-specific dependency graphs out of the core `gentest` runtime/test runner.

## Scope (must-do)

### Model layer

- Extend the generator model (`tools/src/model.hpp`) with an engine-neutral representation, e.g.:
  - `FuzzTargetInfo`:
    - stable name (suite/name),
    - qualified symbol to call,
    - signature classification (bytes vs typed),
    - optional domain/seed metadata stored as strings/AST-derived tokens,
    - origin info (file/line).

### Emission layer

- Emit fuzzing artifacts into a separate generated file (or set of files), distinct from `test_impl.cpp`.
- Choose a backend selector mechanism that is build-system friendly:
  - CMake option (e.g., `GENTEST_FUZZ_BACKEND=fuzztest|none|...`)
  - or separate helper functions that pick the backend implicitly.

### Guardrails

- If fuzz targets exist but no backend is enabled, fail at configure/generate time with a clear message
  (do not silently drop fuzz targets).
- Ensure the backend boundary is the only place engine headers are included.

## Out of scope (nice-to-have)

- Cross-backend compatibility layer for domains/seeds beyond trivial defaults.
- Multi-backend builds in one configure (e.g., generating both FuzzTest and a future engine simultaneously).

## Acceptance criteria

- `gentest_codegen --check` validates fuzz targets without requiring any fuzz backend.
- Generated fuzzing TU(s) include engine headers only when the backend is enabled.
- Switching backend selection does not require changing user-authored source files.

## Notes / references

- Backend v1 is expected to be FuzzTest (macro-free registration API), see the FuzzTest integration story.

