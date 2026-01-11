# Fuzzing (FuzzTest/Centipede) integration: MCP review (2026-01-11)

This is a durable, repo-local review of the first fuzzing iteration on branch `fuzzing-centipede` (commit `ab1358d`).

Source: MCP Codex DAG review job `454b2e8c99cb6971` (2026-01-11). The leaf reports are checked in under `docs/reviews/` for future maintainers/AI agents.

## Key outcomes (what’s already good)

- Public headers stay engine-neutral (no `fuzztest` includes); guarded by the `gentest_public_headers_no_fuzztest` test.
- Fuzzing is opt-in: fuzz-related code and dependencies only appear when a project uses the fuzz codegen + backend helper.

## Top edge cases / risks

1) **Silent (or easy-to-miss) discovery drops**
   - Functions not written in the “main file” (e.g., in headers) are skipped without a clear error.
   - Attribute scanning requires adjacency and can miss attributes when specifiers/macros intervene.
   - Malformed attribute syntax may be ignored without a hard failure.

2) **Signature portability issues**
   - `std::span` detection is strict to `std::span` and can fail under libc++ inline namespaces (`std::__1::span`).
   - Byte-based signatures are limited to `unsigned char` (no `std::byte` support).

3) **Sanitized name collisions**
   - FuzzTest suite/test names are sanitized to identifier-like strings; different display names can collide after sanitization.

4) **Templates**
   - Function templates can currently slip through discovery but will fail to compile when called from generated wrappers.

5) **FuzzTest internal API usage**
   - The generated TU includes `fuzztest/internal/*` headers and uses internal registration APIs, which can be brittle across versions and packaging styles.

6) **CMake strictness + portability**
   - The helper hard-fails when `fuzztest` isn’t found, and assumes specific exported target names.
   - No cross-compiling or Windows-specific fallback/skip behavior is provided yet.

## Recommended next patches (small + high impact)

- Discovery/validation (`tools/src/discovery.cpp`, `tools/src/parse.cpp`, `tools/src/validate.cpp`)
  - Emit explicit diagnostics when skipping due to `isWrittenInMainFile` / adjacency/macro limitations.
  - Reject function templates for fuzz targets with a clear error.
  - Improve `std::span` detection to tolerate libc++ inline namespaces; consider accepting `std::byte` for byte-based fuzz targets.
  - Detect and error on sanitized `(suite,test)` collisions before emitting FuzzTest registrations.

- Emission (`tools/src/templates.hpp`, `tools/src/emit.cpp`)
  - Prefer public FuzzTest APIs (avoid `fuzztest/internal/*` headers) or add `__has_include` fallbacks.
  - Add compile-time “type supported by FuzzTest domain” checks for typed fuzz targets.

- CMake/tests (`cmake/GentestCodegen.cmake`, `tests/CMakeLists.txt`)
  - Add knobs like `GENTEST_ENABLE_FUZZTEST` / `GENTEST_REQUIRE_FUZZTEST` and support a graceful “fuzztest missing” skip path.
  - Add at least one build/link validation test for a fuzz target (compile-only is enough).

## Future fuzz attributes (domains / seeds / corpus)

Today, validation is a strict allowlist, so any new fuzzing attributes will hard-error until the model + validator + emitter are extended.

To evolve this cleanly:
- Add a `FuzzTargetConfig` to the model (`tools/src/model.hpp`).
- Parse config attributes into that model, and merge namespace defaults with per-function overrides where appropriate.
- Keep validation strict, but make the allowlist versioned and documented to avoid accidental “silent ignore”.

## Linked reports

- Discovery/validation: `docs/reviews/fuzzing_mcp_review_codegen_discovery_2026-01-11.md`
- Emission + non-leakage: `docs/reviews/fuzzing_mcp_review_emit_templates_nonleakage_2026-01-11.md`
- CMake + tests: `docs/reviews/fuzzing_mcp_review_cmake_tests_2026-01-11.md`

## Related durable docs

- Long read: `docs/fuzzing/fuzztest_centipede_long_read.md`
- Stories: `docs/stories/003_fuzzing_research.md`, `docs/stories/004_fuzzing_fuzztest_integration.md`, `docs/stories/007_fuzzing_fuzztest_backend.md`
