# Story: Reject producer-reuse local CI optimization

## Context

The local CI optimization work on `codex/local-ci-runtime-optimization`
demonstrated a large wall-time reduction, but the speedup is tied to a producer
reuse layer that is still keyed by a hand-maintained semantic fingerprint.

Follow-up review found that the remaining correctness risks are in the reuse
mechanism itself rather than in the low-risk test-harness parallelism changes.

## Decision

Do not merge the branch as-is.

The current producer-reuse implementation should be rejected unless it is
reworked substantially or removed from the optimization slice.

## Why reject it

### 1. Reuse can still hide real regressions

The producer key still misses meaningful inputs to the producer configure/build
result, including:

- in-place `CMAKE_MAKE_PROGRAM` replacement at the same path
- dependency-discovery inputs such as `CMAKE_PREFIX_PATH` / `fmt_DIR`

That means package-consumer tests can stay green while actually exercising a
stale producer install.

### 2. Proof coverage is weaker than the branch status implies

Some proof tests only verify key-to-directory mapping or can exit early on
skip, so they do not fully prove end-to-end rebuild/reuse correctness for every
claimed producer-key class.

### 3. Maintenance cost increased materially

The branch adds significant complexity to helper scripts and correctness now
depends on continuously keeping the producer fingerprint aligned with all
meaningful build inputs.

That is a poor trade if the project standard is that local CI optimizations must
not sacrifice confidence.

## Coverage assessment

No raw test surface was removed. The risk is effective coverage loss:

- stale producer reuse can bypass validation that would happen on a fresh build
- proof tests can report green without demonstrating the full intended contract

So the branch does not reduce the test count, but it can still reduce the
reliability of what those tests validate.

## Recommended salvage path

Keep only the low-risk improvements in a fresh branch:

- bounded local `ctest` parallelism in `CMakePresets.json`
- helper nested parallel-level forcing in shared helper wrappers
- `xmake` helper `RESOURCE_LOCK`s

Drop the producer-reuse optimization unless there is a willingness to support a
much stricter invalidation model and a broader end-to-end proof suite.

## Next step

Re-measure the safe subset on its own. If it still provides a meaningful local
runtime gain, ship that smaller change and leave producer-reuse optimization
out.
