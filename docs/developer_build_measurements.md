# Measure adoption and edit/build/test costs

Use `scripts/bench_developer_cycle.py` to measure a fresh source-based consumer
and the edits a developer makes to it. This complements the compile-scaling
scripts: configuration, dependency discovery/downloads, generator construction,
runtime construction, generated mocks, registration, compilation, linking, and
CTest execution are included in the relevant phases.

The workload uses ordinary headers and three authored C++ translation units. Its
nine initial cases cover paired boundary rows, a type matrix, fresh local
fixtures, shared suite setup, and a generated ledger mock. Every case has
independent expected results and works through individual CTest discovery.
The shared fixture is also checked by running all cases in one process.

## Run a measurement

Use a clean Gentest checkout, Python 3.9 or newer, and the
[Linux source-build requirements](install/linux.md#requirements). Choose an
unused work directory on a filesystem representative of your development
environment. The script refuses an existing directory and never edits the
Gentest checkout. Each sample gets a fresh copy of the checked-in workload and
its own build directory.

```bash
python3 scripts/bench_developer_cycle.py \
  --source "$PWD" \
  --work-dir /tmp/gentest-cycle-current \
  --samples 3 --jobs 2 --codegen-jobs 1 \
  --cc "$(llvm-config --prefix)/bin/clang" \
  --cxx "$(llvm-config --prefix)/bin/clang++" \
  --cmake-arg="-DLLVM_DIR=$(llvm-config --cmakedir)" \
  --cmake-arg="-DClang_DIR=$(llvm-config --cmakedir)/../clang" \
  --machine-note "Idle development machine; default CPU/power policy; no OS cache flush"
```

Replace the machine note with the actual background load and CPU/power policy.
The build uses Ninja, Release, two build processes and one codegen worker by
default. `--build-type`, `--jobs`, and `--codegen-jobs` make those choices explicit.
`--samples 1` is useful for validating the harness; one observation does not
establish a performance difference. `--allow-dirty` records a dirty checkout as
a reproducibility limitation; commit changes before collecting comparison data.

The source-based consumer uses `add_subdirectory` and builds its own generator
and runtime. Source checkout/download time, OS package installation, package
installation into a prefix, and the repository's unrelated tests are outside
this measurement. Use the [first-use walkthrough](install/linux.md) separately
to validate an installed-package consumer.

## Scenarios and phase boundaries

Each sample runs this fixed, cumulative sequence:

| Scenario | Change | Expected inventory |
| --- | --- | --- |
| `cold` | Configure a fresh consumer/build tree, construct `gentest_codegen`, construct `gentest_main` and its runtime dependencies, build the fixture/mock consumer, and run CTest. | 9 cases |
| `no_op` | Build and run the unchanged consumer. Any Ninja build action is an error. | 9 cases |
| `body_edit` | Change an out-of-line case body from `6 + 8` to `5 + 9`, retaining its independent expected value `14`. | 9 cases |
| `added_case` | Add an annotated header declaration and out-of-line implementation. | 10 cases |
| `added_row` | Add the paired normalization row `(50, 50)`. | 11 cases |
| `production_header_edit` | Refactor the common production header from `std::clamp` to equivalent `std::min`/`std::max`; it is consumed by all three authored translation units and the mock definition. | 11 cases |

The cold run records configure/dependencies, host-generator build, runtime build,
consumer build, and CTest wall times separately. Their sum is `adoption_seconds`.
Later edits use the normal single consumer build command, so their measured
rebuild scope includes any CMake regeneration and codegen that Ninja requires.
`developer_loop_seconds` is consumer build plus CTest wall time for every scenario.

Runner inventory, CTest JSON inventory, and a complete single-process run are
additional correctness checks with their own recorded timings. They are excluded
from the adoption and developer-loop totals. Both inventories must exactly match
the independently specified case names, including new cases and rows. A failed
command, missing/duplicate case, vacuous inventory, or edit that triggers no
rebuild stops the run and leaves a nonzero status and partial results.

Ninja action timings distinguish CMake regeneration, dependency construction,
host-generator construction, runtime construction, mock generation,
registration generation/compilation, other
authored/mock compilation, and consumer linking. Multi-output edges count once.
These are sums of action elapsed times: parallel actions overlap, so they must
not be added to wall time or interpreted as a critical path. The exact command
phase wall times are the end-to-end evidence.

## Caches, metadata, and comparisons

Every sample starts without consumer objects, generated artifacts, or a local
FetchContent dependency directory. Compiler launchers are disabled and
`CCACHE_DISABLE=1` is set. System dependency installations and the OS page cache
are retained. This is a **cold build tree**, not a cold filesystem or a machine
without LLVM/fmt packages. Dependency downloads occur during configuration only
when CMake cannot find an installed fmt package.

`results.json` records the Gentest and harness revisions, dirty status, exact
commands and return codes, tool/compiler versions, selected CMake cache,
LLVM/fmt versions, machine/CPU information, visible cgroup CPU quota, per-scenario
load averages, cache policy, phase wall times,
Ninja actions, verified case names, and any failure. Per-command logs remain in
each sample's `logs/` directory. Keep samples and logs while assessing failures,
variance, and background load; publish run-specific results in the PR, not in
this guide.

For a comparison, create clean baseline and candidate worktrees and run this
same harness and workload with `--source` pointing at each one. Keep compiler,
dependencies, build flags, workload, filesystem, and job counts the same. Run
timing commands sequentially on the same machine and interleave whole baseline
and candidate samples using new work directories. Record the run order and any
background load. The harness revision and the measured source revision are
reported separately so an older source can use the same measurement code.

Report absolute times, sample count, and variation for each scenario and phase.
Investigate failures before interpreting timing. For paired comparable runs,
elapsed-time speedup is baseline/candidate and time reduction is
`100 * (1 - candidate/baseline)`. Propose optimizations from measured phases and
rebuild actions; validate a chosen change with fresh comparable runs before
claiming an improvement.
