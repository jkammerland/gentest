# Reproducible Compile Benchmark Campaign

`scripts/bench_compile_campaign.py` is the current compile-time measurement
harness. Its baseline contract is current master commit
`9edd3c826eadb31714f6462b5264cc1793bb535b`; it does not embed lab results or
turn wall time into a pass/fail threshold.

The harness creates a generated consumer fixture, isolated build/cache
directories, and (for a clean checkout) an ephemeral detached worktree. It
does not modify source files in the checkout. Full dirty/untracked state is
recorded in provenance; tracked staged/unstaged changes are rejected unless
`--allow-dirty` is explicit. Untracked local tooling noise does not prevent an
isolated HEAD worktree. Every run requires an empty output directory so a
failed run cannot leak configured build trees or cache state into a retry.

## Run

Clang 22 is the primary baseline and GCC 16 is the secondary baseline. Pin
both compiler paths: the script reports an unavailable requested compiler and
never substitutes another compiler.

```bash
python3 scripts/bench_compile_campaign.py \
  --cc clang-22 --cxx clang++-22 \
  --output-dir build/compile-campaign-clang22

python3 scripts/bench_compile_campaign.py \
  --cc gcc-16 --cxx g++-16 \
  --output-dir build/compile-campaign-gcc16
```

For a short exploratory run, reduce samples and scope lanes explicitly:

```bash
python3 scripts/bench_compile_campaign.py \
  --cc clang-22 --cxx clang++-22 \
  --samples 1 --warmups 0 \
  --lanes one-tu,eight-tu-one-binary,eight-tu-eight-binary \
  --output-dir build/compile-campaign-smoke
```

The default campaign runs two warmups then seven timed samples. It covers:

- one generated test TU;
- eight test TUs in one binary;
- eight test TUs as eight binaries;
- representative repository E2E targets;
- a standalone `gentest_runtime` build;
- codegen caps `1,2,4,8,auto` (`auto` is tool value `0`).

For every codegen-cap sweep, sample rounds alternate forward/reverse order to
reduce thermal and ordering bias. Synthetic fixture scenarios are cold build,
no-op, CMake reconfigure, source edit, private-header edit, shared-header
edit, and an equivalent `compile_commands.json` rewrite. The harness captures
new Ninja edges for each scenario and asserts its invalidation contract. It
also measures a rewrite that adds only an unrelated compilation-database
entry. Mutated files are restored byte-for-byte (including timestamps) and a
zero-edge settling build is required after every sample, so later caps and
lanes receive identical fixture inputs. The built binaries are run once only
as a correctness gate, never as part of a timed build sample.

The generated fixtures require a true zero-edge no-op and reconfigure follow-up
build. The repository E2E lane separately records the current persistent
codegen-only depfile edge (and rejects any compile or link edge) so the
campaign reports that real invalidation behavior instead of mislabelling it as
a clean no-op.

Compiler cache modes are isolated per result directory:

```bash
python3 scripts/bench_compile_campaign.py --cc clang-22 --cxx clang++-22 \
  --cache ccache --output-dir build/compile-campaign-clang22-ccache
```

`--cache` accepts `off` (the default), `ccache`, and `sccache`. Selecting a
missing cache program fails clearly; no user-level cache directory or config
is changed.

## Result schema and interpretation

Each output directory contains `result.json` and `summary.md`.

`result.json` schema version 1 contains:

- `provenance`: machine/OS/CPU/core/RAM data, Git HEAD and dirty state,
  current-master baseline relation, CMake/Ninja/compiler versions and paths,
  Release host-codegen identity, selected generator/build type, and relevant
  `GENTEST_`, cache, and CMake environment controls;
- `cache`: selected mode, isolated directory, and before/after tool stats;
- `configuration`: raw sample count, warmups, build jobs, requested codegen
  caps, and the actual alternating execution order;
- `lanes[]`: lane/cap identity, effective CLI caps found in `build.ninja`,
  build/target paths, and per-scenario results;
- `lanes[].scenarios.<name>.samples_s`: every raw timed measurement;
- `median_s` and `mad_s`: median and median absolute deviation of those raw
  values; use these to describe variability, not to erase the raw samples;
- `profiles`: captured Ninja edge counts/categories used to verify each
  invalidation scenario.

Compare raw samples and provenance before comparing medians. CPU frequency,
thermal state, disk/cache state, host codegen binary, compiler version, and a
dirty checkout can all materially change a compile-time result.

## Legacy direct benchmark

`scripts/bench_compile.py --codegen-jobs=<N>` rewrites the already configured
Ninja `gentest_codegen --jobs=<N>` command tokens instead of relying on
`GENTEST_CODEGEN_JOBS` after configuration. It records both requested and
asserted effective values in JSON and fails if a codegen command has no
assertable `--jobs=` option. For cold target samples, the reported target time
is explicitly end-to-end and includes codegen; it is not added to the separate
component time. Temporary `build.ninja` cap rewrites are always rolled back,
including after a failed or interrupted benchmark.
