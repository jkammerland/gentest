# Measure adoption and edit/build/test costs

`scripts/bench_developer_cycle.py` measures two things for a small project that
builds gentest from source: how long it takes to configure, build and run its
first tests (adoption), and how long the edit, rebuild and test loop takes
after typical changes. Use it to compare gentest revisions and to see which
build phase a change makes slower.

## Run a measurement

You need a clean gentest checkout, Python 3.9 or newer, and the build
requirements from the [Linux install guide](install/linux.md). Choose a work
directory that doesn't exist yet, on a filesystem like the one you develop on:

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

Replace the machine note with the real background load and CPU or power
policy, and use the same LLVM for every run you compare.

The script never edits the checkout given by `--source`. Each sample copies the
workload into `<work-dir>/sample-NN/consumer/` and builds it in its own build
directory. At the end the script prints the median and range of build plus
CTest time for each scenario, and the median adoption time.

| Option | Default | Meaning |
| --- | --- | --- |
| `--source DIR` | the checkout that contains the script | gentest source to measure |
| `--work-dir DIR` | required | Directory for the samples and `results.json`; it must not exist yet |
| `--samples N` | 3 | Number of samples. One sample checks that the harness works; it doesn't establish a performance difference. |
| `--jobs N` | 2 | Parallel build processes |
| `--codegen-jobs N` | 1 | `GENTEST_CODEGEN_JOBS` for the consumer configure (0 = auto) |
| `--build-type TYPE` | `Release` | `Debug`, `Release` or `RelWithDebInfo` |
| `--cc`, `--cxx` | `clang`, `clang++` | Compilers for the build |
| `--cmake-arg=ARG` | none | Extra configure argument; repeat it for more |
| `--allow-dirty` | off | Measure a checkout with uncommitted changes, and record that as a reproducibility limitation. Commit your changes before collecting comparison data. |
| `--machine-note TEXT` | required | Machine conditions recorded with the results |

The build always uses Ninja.

The workload adds gentest with `add_subdirectory`, so the first scenario builds
`gentest_codegen` and the runtime from source. Downloading gentest, installing
packages or an installed gentest, and gentest's own tests aren't measured.

## The workload

The workload in `scripts/bench_workloads/developer_cycle/` is a small CMake
project with ordinary headers and three source files (`cases.cpp`,
`fixtures.cpp`, `mock_cases.cpp`). Its nine cases cover a table of boundary
rows, a template type matrix, local fixtures, a shared suite fixture and a
generated mock (`gentest_add_mocks()`). Every case checks an expected result
that the workload states independently. `gentest_discover_tests()` makes each
case its own CTest test, and one run of the whole executable also checks the
shared suite fixture.

## Scenarios and phase boundaries

Each sample runs these scenarios in order. The edits accumulate.

| Scenario | Change | Expected cases |
| --- | --- | --- |
| `cold` | Configure a fresh build tree, build `gentest_codegen`, build `gentest_main` and its runtime dependencies, build the workload with its mock, and run CTest. | 9 |
| `no_op` | Build and run the unchanged workload. Any Ninja build action is an error. | 9 |
| `body_edit` | Change an out-of-line case body from `6 + 8` to `5 + 9`; the expected value `14` stays. | 9 |
| `added_case` | Add an annotated declaration to the header and its out-of-line definition. | 10 |
| `added_row` | Add the row `(50, 50)` to the row table. | 11 |
| `production_header_edit` | Rewrite the shared production header from `std::clamp` to the equivalent `std::min`/`std::max`. All three source files and the mock definitions header include it. | 11 |

The `cold` scenario records separate wall times for configure (including
dependency discovery and downloads), the `gentest_codegen` build, the runtime
build, the workload build and CTest. Their sum is `adoption_seconds`. The later
scenarios use the normal single build command for the workload, so their
rebuild time includes any CMake regeneration and codegen that Ninja needs.
`developer_loop_seconds` is the workload build plus CTest wall time, for every
scenario.

Each scenario also checks its results; these checks are timed but not counted
in `adoption_seconds` or `developer_loop_seconds`:

- the case list from the test executable (`--list-tests`) and the CTest test
  list (`ctest --show-only=json-v1`) must both match the expected case names
  exactly, including added cases and rows;
- one run of the whole executable must pass.

A failed command, a missing or duplicate case, an empty inventory, or an edit
that triggers no rebuild stops the run. The script then exits non-zero and
keeps the partial results.

From `.ninja_log`, the script also sums the build actions in each category
(CMake regeneration, dependency builds, the `gentest_codegen` build, the
runtime build, mock generation, registration generation and compilation, other
compilation, and linking). Parallel actions overlap, so these sums don't add up
to wall time; the wall times of the timed commands are the end-to-end
measurement.

## Caches and recorded data

Every sample starts without workload objects, generated files or a local
FetchContent dependency directory. Compiler launchers are disabled and
`CCACHE_DISABLE=1` is set. System packages and the OS page cache stay as they
are. The measurement starts from a **cold build tree**, not a cold filesystem
or a machine without LLVM or fmt. Configure downloads fmt only when CMake
can't find an installed fmt package.

`<work-dir>/results.json` records:

- the gentest and harness revisions and whether each checkout was dirty;
- the exact commands and their return codes;
- tool and compiler versions, the selected CMake cache, and the LLVM and fmt
  versions;
- machine and CPU information, the visible cgroup CPU limit, and load averages
  before and after each scenario;
- the cache policy, the phase wall times, the Ninja actions, the verified case
  names, and any failure.

Each sample keeps its per-command logs in `logs/`. Put the results of a
particular run in the pull request, not in this page.

## Compare two revisions

1. Create clean worktrees of the baseline and the candidate.
2. Run the same script and workload with `--source` pointing at each worktree.
   The results record the harness revision and the measured source revision
   separately, so an older source can be measured with the same harness.
3. Keep the compiler, dependencies, build flags, workload, filesystem and job
   counts the same.
4. Run the timings one after another on the same machine, alternating whole
   baseline and candidate samples, each with a fresh work directory. Record
   the run order and any background load.

Report absolute times, the number of samples and the variation for each
scenario and phase. The speedup is baseline/candidate and the time reduction is
`100 * (1 - candidate/baseline)`. Confirm a change with fresh comparable runs
before you claim an improvement.

## Other measurement scripts

| Script | What it measures |
| --- | --- |
| `scripts/bench_compile.py` | In an existing build tree: the `gentest_codegen` build, each codegen command and each test target build; writes `compile_bench.json` |
| `scripts/bench_compare.py` | Compares the `bench_compile.py` results of two build directories or presets side by side, running it where needed |
| `scripts/bench_clean.py` | Configures a clean build directory and runs `bench_compile.py` |
| `scripts/bench_case_scale.py` | Rebuild time of a one-file test project as the number of cases grows, against GoogleTest and doctest |
| `scripts/bench_case_layout_scale.py` | The same total number of cases spread over several source files or several test executables |
| `scripts/bench_gtest_compare.py` | End-to-end build time of the same tests written for gentest, GoogleTest and doctest |
| `scripts/bench_release_e2e_compare.py` | Release build time of representative test executables against a base ref, excluding the `gentest_codegen` build |
| `scripts/profile_codegen.sh` | CPU profile (`perf`) and peak memory of every `gentest_codegen` run in a build |

Each script's `--help` lists its options.
