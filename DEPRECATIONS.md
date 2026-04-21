# gentest deprecations

This file tracks gentest surfaces that are deprecated, removed, or scheduled
for removal. It is intentionally short: detailed design and rollout context
lives in the linked story.

Version fields mean:

- `Deprecated since`: the first version whose docs classify the surface as
  discouraged.
- `Warn since`: the first version that emits a runtime, configure, or build
  warning. `N/A` means the item is internal or already absent from the current
  supported package surface.
- `Removal target`: the earliest version or gated release window where removal
  may hard-fail or delete the old path.

## User-facing deprecations

| Feature | Status | Deprecated since | Warn since | Removal target | Replacement | Owner |
| --- | --- | --- | --- | --- | --- | --- |
| `gentest_codegen --output <file>` legacy single-TU manifest mode | Removed; hard-fails with migration guidance | `1.0.0` | `1.0.0` | Removed in `2.0.0` | TU-wrapper mode with `--tu-out-dir` and explicit per-input outputs | [story 037](docs/stories/037_codegen_contract_cleanup_campaign.md) |
| `gentest_attach_codegen(... OUTPUT <file>)` legacy single-TU manifest mode | Removed; configure hard-fails with migration guidance | `1.0.0` | `1.0.0` | Removed in `2.0.0` | Default TU-wrapper mode, or `OUTPUT_DIR <dir>` when a concrete generated-output directory is needed | [story 037](docs/stories/037_codegen_contract_cleanup_campaign.md) |
| `NO_INCLUDE_SOURCES` / `--no-include-sources` / `GENTEST_NO_INCLUDE_SOURCES` legacy manifest-mode escape hatch | Removed with legacy manifest mode | `1.0.0` | `1.0.0` | Removed in `2.0.0` | No replacement; use TU-wrapper mode so owner sources stay in normal compilation units | [story 037](docs/stories/037_codegen_contract_cleanup_campaign.md) |
| `gentest_codegen --template <file>` legacy manifest-mode template override | Removed with legacy manifest mode | `1.0.0` | N/A | Removed in `2.0.0` | No replacement; generate TU registration headers with the built-in protocol | [story 037](docs/stories/037_codegen_contract_cleanup_campaign.md) |
| `EXPECT_SUBSTRING` in `gentest_discover_tests(...)` | Removed; configure hard-fails with replacement guidance | `1.0.0` | `1.0.0` | Removed in `2.0.0` | `DEATH_EXPECT_SUBSTRING` | [story 037](docs/stories/037_codegen_contract_cleanup_campaign.md) |

## Removed or internal cleanup inventory

These rows are not public compatibility promises. They are tracked here so
cleanup work is visible and regression coverage can prevent old implementation
shapes from returning.

| Item | Status | Deprecated since | Warn since | Removal target | Replacement | Owner |
| --- | --- | --- | --- | --- | --- | --- |
| Legacy installed `share/cmake/gentest/scan_inspector/` helper directory | Removed from the source package; guarded by package-consumer install smoke | `1.0.0` | N/A | Guarded in story 037 wave 4 | Tool-owned source inspection through `gentest_codegen --inspect-source` and mock/artifact manifest validation | [story 037](docs/stories/037_codegen_contract_cleanup_campaign.md) |
| Configure-time source-inspector probe helpers in `GentestCodegen.cmake` | Internal cleanup scheduled | `1.0.0` | N/A | First release after story 033 closes | Artifact manifest module fields, mock-manifest module metadata, tool-side validation, and manifest-declared textual wrapper semantics | [story 037](docs/stories/037_codegen_contract_cleanup_campaign.md) |
| CMake scan include-dir and macro collection helpers | Internal cleanup scheduled | `1.0.0` | N/A | First release after story 033 closes | Tool-side `compile_commands.json` scans | [story 037](docs/stories/037_codegen_contract_cleanup_campaign.md) |
| `xmake/templates/*.in` and `meson/*.in` generated-source skeletons | Wrapper cleanup scheduled | `1.0.0` | N/A | First release after story 015 closes | Final generated sources emitted by `gentest_codegen` | [story 037](docs/stories/037_codegen_contract_cleanup_campaign.md) |
| `xmake/gentest.lua` scan and emit planning logic | Wrapper cleanup scheduled | `1.0.0` | N/A | First release after story 015 closes | Thin manifest consumer plus file staging | [story 037](docs/stories/037_codegen_contract_cleanup_campaign.md) |
| `build_defs/gentest.bzl` scan and emit planning logic | Wrapper cleanup scheduled | `1.0.0` | N/A | First release after story 015 closes | Thin manifest consumer plus file staging | [story 037](docs/stories/037_codegen_contract_cleanup_campaign.md) |
