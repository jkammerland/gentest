# Story: Codegen no .cpp includes + auto sources

## Goal

Stop the generated test implementation from `#include`-ing implementation sources, and make codegen discover scan sources from the target by default.

## Motivation / user impact

- `#include`-ing `.cpp` files in generated code hides missing headers and can introduce ODR/build-order hazards.
- Requiring users to list scan sources separately from the target is error-prone and easy to drift.
- The generated TU should see the same headers and declarations as the normal target sources.

## Scope (must-do)

1. **No `.cpp` includes in generated output**
   - Add a codegen switch (and CMake option) to skip emitting `#include` directives for scanned sources.
   - Emit a generated `*_test_decls.hpp` header that contains required `#include`s and forward decls, and include it from `test_impl.cpp`.

2. **Auto source discovery in `gentest_attach_codegen()`**
   - When `SOURCES` is not supplied, collect scan sources from `TARGET_PROPERTY:SOURCES`.
   - Compile scanned sources as normal TUs and only add the generated output to the target when auto-sourcing.

3. **Guardrails**
   - Keep output collision checks and preserve explicit `SOURCES` flows for non-standard layouts.

## Out of scope (follow-ups)

- CI guard that fails if generated output includes `.cpp` files.
- Multi-root include discovery (beyond the current root).
- Making the declaration-only mode the default for all consumers without opt-in.

## Acceptance criteria

- Generated `test_impl.cpp` includes the generated declarations header and does not `#include` scanned `.cpp` sources.
- `gentest_attach_codegen(target)` succeeds without explicit `SOURCES` for in-tree tests.
- `cmake --preset=debug-system && cmake --build --preset=debug-system && ctest --preset=debug-system --output-on-failure` passes.
