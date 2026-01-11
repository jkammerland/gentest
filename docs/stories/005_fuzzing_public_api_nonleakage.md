# Story: fuzzing API surface (no engine leakage)

## Goal

Define and implement a `gentest` fuzzing authoring API that:

- is attribute-driven (discovered by `gentest_codegen`),
- does **not** leak any engine-specific types/includes (FuzzTest or future engines) into `gentest` public headers,
- keeps fuzzing **opt-in**: no fuzz code is compiled/linked unless consumers enable it.

## Motivation / user impact

- Consumers want a stable, `gentest`-native way to declare fuzz targets without committing to a specific engine API.
- The engine may change over time (FuzzTest/Centipede today; a `gentest` engine later). Public headers must remain stable.

## Scope (must-do)

### Public authoring surface (headers)

- Add fuzzing attribute documentation to `include/gentest/attributes.h` only.
  - Example: `[[using gentest: fuzz("suite/name")]]`
  - Optional follow-ups: `domain(...)`, `seed(...)`, `corpus_database(...)`, `time_budget(...)`, etc.
- Do **not** add any `#include <fuzztest/...>` or `fuzztest::` references anywhere under `include/gentest/`.
- Do **not** add runtime APIs in `include/gentest/runner.h` that mention fuzzing engines.

### Generator semantics (discovery + validation)

- `gentest_codegen` must detect fuzz targets as a separate kind from tests/benches.
- Validation must be strict enough to prevent silent “no code generated” situations:
  - If a declaration has fuzz-only attributes (e.g., future `domain(...)`) but is missing `fuzz(...)`, it should be an error.
  - If `fuzz(...)` is present but the signature is unsupported, error with a clear message.
- Specify supported v1 signatures (pick one minimal set):
  - bytes: `void f(std::span<const std::uint8_t>)` (or `(const std::uint8_t*, std::size_t)`), and/or
  - typed: `void f(T1, T2, ...)` with engine-dependent domain mapping in the backend story.

### Non-leakage guardrails

- Add a small CI/test guard that fails if `include/gentest/` contains:
  - `fuzztest/` includes
  - `fuzztest::` identifiers
  - engine-specific build flags in public headers

## Out of scope (nice-to-have)

- Implementing the FuzzTest backend (tracked separately).
- Implementing a `gentest`-native fuzzing engine.
- Perfect feature parity across engines.

## Acceptance criteria

- Public headers under `include/gentest/` contain **no** FuzzTest includes or symbols.
- `gentest_codegen --check` errors (with actionable diagnostics) on:
  - fuzz metadata without `fuzz(...)`,
  - unsupported fuzz target signatures.
- Existing `gentest` (non-fuzzing) builds/tests remain unchanged unless fuzzing is enabled.

## Notes / references

- Engine integration is intentionally deferred; see the backend story for FuzzTest/Centipede wiring.

