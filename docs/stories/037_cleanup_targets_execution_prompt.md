# Cleanup Targets Execution Prompt

Inject this prompt into a fresh Codex session when starting the cleanup campaign.

```text
You are working in the gentest repository. Your goal is to simplify the codebase by deleting duplicated buildsystem/codegen/runtime plumbing and moving source-scanning responsibility into `gentest_codegen`. Removing code is preferred when behavior and coverage remain intact.

Hard requirements:

- Work incrementally. Do not attempt multiple large targets in one patch.
- Use separate git worktrees for big targets that may conflict.
- Make one tested commit per coherent increment.
- Before implementing each increment, run a read-only batch Codex review with `gpt-5.4-xhigh` to validate the plan and identify risks.
- For implementation, use iterative Codex when the increment is non-trivial. Require it to run tests and create a clear PASS marker only after implementation, review, and verification are complete.
- After implementation, run another read-only batch Codex review against the diff. Address blocking findings before committing.
- Run formatting and targeted tests on every increment. Run broader tests when touching CMake/codegen/buildsystem behavior.
- Keep the simplest correct implementation. Prefer deleting legacy paths over adding compatibility layers.
- Do not use generated/runtime output as the oracle for tests that validate that same output. Discovery and inventory tests need independent checked-in expectations or explicit expected values.
- Do not derive expected counts from `--list` when testing discovery/listing correctness. That is circular.
- Preserve public behavior unless the target explicitly says deprecate/remove, and document any intentional public behavior change in the story/changelog.

Default local verification commands:

- `cmake --preset=debug-system`
- `cmake --build --preset=debug-system`
- `ctest --preset=debug-system --output-on-failure -R '<targeted-regex>'`
- `scripts/check_clang_format.sh`
- `scripts/check_clang_tidy.sh build/debug-system` after configuring a Clang-based debug-system build

Use platform verification when relevant:

- For Windows/path/buildsystem changes, validate on the Windows machine or CI-equivalent Windows job.
- For macOS/module/toolchain changes, validate on macOS or CI-equivalent macOS job.
- For non-CMake buildsystems, run the relevant CTest contract checks and direct buildsystem smoke tests where practical.

Batch Codex review pattern:

- Use `batch_codex_async`.
- Use `model: "gpt-5.4-xhigh"`.
- Use `sandboxMode: "read-only"` for review.
- Ask reviewers for blocking findings only, with file/line references.
- Ask each review task to write any detailed notes to `results/<target>-review.md` and return a short summary.

Iterative Codex implementation pattern:

- Use `iterative_codex_async`.
- Use `model: "gpt-5.4-xhigh"`.
- Use `sandboxMode: "workspace-write"`.
- Set `maxRounds` high enough for implementation plus review fixes.
- Define a target-specific pass marker, e.g. `PASS_TARGET_025_CMAKE_LOGIC_REDUCTION`.
- Instruct the worker to create the pass marker only after code changes, formatting, targeted tests, and a self-review pass are complete.

Target table:

| Priority | Target | Remove / simplify | Acceptance criteria |
| --- | --- | --- | --- |
| High | `cmake/GentestCodegen.cmake` | Remove CMake-owned source inspection, compile-definition evaluation, module/source rewriting, and parser-like logic. CMake should invoke `gentest_codegen` and consume explicit artifacts only. | CMake no longer evaluates source semantics that clang tooling should own. Existing CMake consumer tests pass. Non-CMake contracts remain intact. |
| High | Configure-time scan helpers / `scan_inspector` path | Delete configure-time source scanning once codegen owns scan/artifact planning. | No buildsystem bootstrapping step is needed to inspect test sources. Codegen remains the only source scanner. |
| High | Legacy manifest/single-TU mode | Decide whether `gentest_attach_codegen(... OUTPUT ...)` / `gentest_codegen --output` remains supported. If not, deprecate and remove the parallel emission path in a staged way. | There is one preferred registration model, with clear migration docs and tests. |
| High | Module-wrapper mock injection | Story `035` closes same-module registration with split `inspect-mocks` / `--mock-registration-manifest`; the remaining cleanup is retiring legacy module-wrapper source transformation, namespace close/reopen hacks, `--module-wrapper-output`, and related CMake/Xmake plumbing after wrapper-mode users migrate. | Module tests register natively from generated artifacts without final include `.cpp` hacks. Mock extraction and mock rendering can run as separate codegen phases, and any remaining wrapper-mode support is explicitly legacy. |
| High | Public generated-runtime compatibility shims | Shrink/remove generated-code-only runtime internals from installed public headers such as `fixture.h` and `registry.h`. | Normal consumers no longer see internal registration/allocation plumbing. Generated code still builds via explicit detail/private surfaces. |
| High | CMake JSON/artifact validation scripts | Move deep artifact schema/contract validation into `gentest_codegen`; leave CMake with command/file existence checks. | CMake is not a second JSON schema interpreter. Artifact validation remains covered independently. |
| Medium | `tools/src/main.cpp` | Split command dispatch from implementation, then remove legacy aliases and duplicated phase plumbing after callers are updated. | Main entrypoint is smaller and mostly dispatch/orchestration. Codegen behavior remains covered by CLI tests. |
| Medium | `tools/src/scan_utils.hpp` | Split by responsibility, then delete scan/build flag helpers that only exist to support buildsystem-side planning. | Scan utilities map to codegen-owned responsibilities, not buildsystem compatibility hacks. |
| Medium | `scripts/gentest_buildsystem_codegen.py` | Decide whether this remains the shared non-CMake adapter. If direct artifact plans replace it, delete it. | There is not a second Python implementation of the artifact protocol unless explicitly justified. |
| Medium | `xmake/gentest.lua` | Remove hand-built filename/path/protocol planning once Xmake can consume codegen-declared artifacts. | Xmake composes generated artifacts instead of duplicating codegen planning logic. Xmake textual/module/xrepo tests pass. |
| Medium | Test helper sprawl | Consolidate repeated CMake script-test wrappers while keeping independent expected files/counts where those tests validate discovery. | Boilerplate drops without circular test oracles. Checked-in expectations remain independent of program output. |
| Medium | `include/gentest/mock.h` | Separate minimal public mock API from generated mock registration/implementation internals. Remove auto-include compatibility macros once explicit artifacts are standard. | Public mock use does not expose generated-code machinery. Existing mock tests and package consumers pass. |
| Low | Deprecated compatibility APIs | Remove old aliases/options after a changelog/deprecation note. | Stale behavior no longer constrains new design. Users get a clear migration note. |
| Low | Historical story/progress docs | Keep accepted design docs and status summaries; archive or delete outdated progress reports. | Story docs reflect current architecture and do not contain stale implementation directions. |
| Low | Workspace/LSP noise | Keep build/toolchain/vendor artifacts out of the repo root and make compile DB setup explicit. | Developer tooling no longer indexes unrelated clang/buildsystem internals by default. |

Execution order:

1. Start with the smallest target that deletes duplicate buildsystem logic without changing public semantics.
2. Prefer targets that improve the codegen-owned artifact model before touching public API cleanup.
3. Keep textual and module behavior clearly separated unless a story explicitly unifies artifact composition.
4. For each target, write or update a story with the exact goal, non-goals, migration notes, and tests.
5. Implement one slice, run review/tests, commit, then reassess the next slice.

Reporting after each commit:

- State commit SHA and branch/worktree.
- List files changed at a high level.
- List exact tests run.
- Summarize batch/iterative Codex review result.
- List known residual risks or explicitly state none found.
```
