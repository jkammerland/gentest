# Story: Codegen include roots + fixture headers

## Goal

Make generated sources hermetic and portable by avoiding absolute-path `#include`s, and validate that fixture types can live in headers such that the generated TU sees the same includes as the target’s other TUs.

## Motivation / user impact

- Absolute paths in generated files leak machine-specific locations and break cacheability.
- The generated TU should compile with the same include dirs as the test target (no “special” include tricks).
- Moving fixture/mock target types into headers enables a future “scan target sources + don’t `#include` .cpp sources” architecture.

## Scope (must-do)

1. **Relative `#include`s for scanned sources**
   - Add a codegen option to compute include strings relative to one or more roots.
   - Wire `gentest_attach_codegen()` to pass a sensible default root and ensure that root is on the target’s include path.

2. **Refactor fixtures into headers (repo tests)**
   - Move fixture type definitions used by the test suites out of `cases.cpp` into suite-local headers.
   - Keep discovery behavior and test counts unchanged.

## Out of scope (follow-ups)

- Default `gentest_attach_codegen(target)` to auto-discover scan sources from `TARGET_PROPERTY:SOURCES`.
- Switching the default runtime model from “generated TU `#include`s case sources” to “case sources compile normally + generated TU only references declarations”.

## Acceptance criteria

- Generated `test_impl.cpp` does not contain absolute-path `#include`s for scanned sources.
- `cmake --preset=debug-system && cmake --build --preset=debug-system && ctest --preset=debug-system --output-on-failure` passes.

