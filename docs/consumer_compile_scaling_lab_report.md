# Consumer Compile Scaling Lab Report

Date: 2026-04-21

Branch: `perf/consumer-generated-tu-compile`

Measured commit: `f7fbdaeea2166bed7ab93318f187484e3716b37e`

Status caveat: the measured runs below were taken from a dirty worktree while the generated-TU compile-weight experiment was in progress. Treat
these as lab measurements for this branch, not a final release benchmark. Rerun the commands in this document from a clean commit before publishing
numbers externally.

## Goal

Measure consumer end-to-end build cost for equivalent test and benchmark workloads across:

- gentest
- GoogleTest
- jkammerland/doctest
- Google Benchmark, for benchmark comparison
- nanobench, for benchmark comparison

The primary question is consumer compile performance. The gentest generator tool build is deliberately excluded. For gentest, the timed section
includes only:

- codegen execution against consumer test sources
- generated test or benchmark translation-unit compilation
- final consumer binary link

For GoogleTest, doctest, Google Benchmark, and nanobench, the timed section includes only:

- consumer test or benchmark translation-unit compilation
- final consumer binary link

Configure time, dependency fetch/build time, `gentest_codegen` tool build time, gentest runtime build time, and helper/main library builds are outside
the timed section.

## Environment

Representative local run:

- OS: Linux
- CMake: `4.3.1`
- Ninja: `1.14.0.git`
- Compiler: `c++ (GCC) 15.2.1 20260123 (Red Hat 15.2.1-7)`
- Build type: `Release`
- Build jobs: `8`
- ccache: disabled by script default via `CCACHE_DISABLE=1`
- GoogleTest: `1.15.2`, from `pkg-config`
- doctest: `https://github.com/jkammerland/doctest.git @ 397ce0086a7ba36524c5706dfc2820527260e65b`
- Google Benchmark: `https://github.com/google/benchmark.git @ 192ef10025eb2c4cdd392bc502f0c852196baa48`
- nanobench: `https://github.com/martinus/nanobench.git @ e4327893194f06928012eb81cabc606c4e4791ac`

The scripts clear `CMAKE_TOOLCHAIN_FILE` so the vcpkg preset path does not affect these harnesses. Set `CC` and `CXX` in the environment before
running the scripts if you need to pin a different compiler.

## Reproduction

The scripts write JSON results under `build/` and print a Markdown-friendly summary to stdout.

### Scaling: gentest vs GoogleTest vs doctest

This benchmark generates one synthetic CMake project per case count. Each project has a single test TU with `N` trivial tests in each framework.
It prebuilds `gentest_codegen`, gentest runtime, and doctest's `main` helper, then repeatedly cleans and rebuilds only the consumer test target.

Exact command used for this report:

```bash
rm -rf build/bench-case-scale
python3 scripts/bench_case_scale.py \
  --counts 1,10,25,50,100,250,500,1000,2000 \
  --samples 3 \
  --jobs 8 \
  --skip-run
```

Output:

```text
build/bench-case-scale/case_scale.json
```

`--skip-run` was used because this report is compile-time focused. Drop it when you want the script to run each generated binary once before the
timed rebuild samples.

### Layout Scaling: Multiple TUs Or Binaries

This benchmark keeps total test count fixed but distributes cases over up to 8 shards:

- `multi-tu`: one target binary, with up to 8 test TUs in that target
- `multi-binary`: up to 8 target binaries, with one test TU per binary

Exact commands used for this report:

```bash
rm -rf build/bench-case-layout-scale/multi-tu-shards-8
python3 scripts/bench_case_layout_scale.py \
  --layout multi-tu \
  --shards 8 \
  --counts 1,10,25,50,100,250,500,1000,2000 \
  --samples 3 \
  --jobs 8 \
  --skip-run

rm -rf build/bench-case-layout-scale/multi-binary-shards-8
python3 scripts/bench_case_layout_scale.py \
  --layout multi-binary \
  --shards 8 \
  --counts 1,10,25,50,100,250,500,1000,2000 \
  --samples 3 \
  --jobs 8 \
  --skip-run
```

Outputs:

```text
build/bench-case-layout-scale/multi-tu-shards-8/case_scale_multi-tu_shards_8.json
build/bench-case-layout-scale/multi-binary-shards-8/case_scale_multi-binary_shards_8.json
```

### Fixed Workload: Existing Repo Tests And Benchmarks

This benchmark builds equivalent framework ports for the existing unit, integration, and skip-only test surface, plus append-iota benchmark binaries.
The test workload has 26 tests and 3 skipped tests. The benchmark workload appends `1,000,000` integers into `std::vector`, `std::list`, and
`std::deque`.

Exact command used for this report:

```bash
rm -rf build/bench-gtest-compare
python3 scripts/bench_gtest_compare.py --samples 5 --jobs 8
```

Output:

```text
build/bench-gtest-compare/gtest_compare.json
```

The script also validates the generated executables once before timing unless `--skip-run` is passed.

To rerun the benchmark binaries after the harness has been configured:

```bash
build/bench-gtest-compare/harness/gentest_compare_benchmarks \
  --kind=bench \
  --bench-epochs=1 \
  --bench-warmup=0 \
  --bench-min-epoch-time-s=0 \
  --bench-min-total-time-s=0 \
  --bench-max-total-time-s=10

build/bench-gtest-compare/harness/gbench_compare_benchmarks --benchmark_format=console

build/bench-gtest-compare/harness/nanobench_compare_benchmarks
```

## Results: Test Count Scaling

Median wall times in seconds. `d/gtest` and `d/doctest` are `gentest - other`; negative means gentest is faster.

| Cases | gentest | codegen | gentest TU | GoogleTest | doctest | d/gtest | d/doctest |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 0.858 | 0.318 | 0.487 | 0.537 | 0.104 | +0.321 | +0.754 |
| 10 | 0.899 | 0.318 | 0.530 | 0.636 | 0.145 | +0.263 | +0.754 |
| 25 | 0.921 | 0.316 | 0.553 | 0.786 | 0.205 | +0.135 | +0.716 |
| 50 | 0.968 | 0.318 | 0.598 | 1.056 | 0.310 | -0.088 | +0.658 |
| 100 | 1.057 | 0.325 | 0.680 | 1.614 | 0.523 | -0.557 | +0.535 |
| 250 | 1.319 | 0.338 | 0.929 | 3.718 | 1.194 | -2.399 | +0.124 |
| 500 | 1.720 | 0.372 | 1.294 | 7.823 | 2.544 | -6.103 | -0.824 |
| 1000 | 2.569 | 0.476 | 2.037 | 17.352 | 5.464 | -14.783 | -2.896 |
| 2000 | 4.668 | 0.891 | 3.719 | 40.503 | 12.327 | -35.835 | -7.659 |

Linear fit across all sampled points:

| Path | Fit |
| --- | ---: |
| gentest e2e | `0.848s + 1.8674ms/case * N` |
| GoogleTest e2e | `-0.398s + 19.7166ms/case * N` |
| doctest e2e | `-0.108s + 6.0439ms/case * N` |
| gentest codegen only | `0.289s + 0.2726ms/case * N` |
| gentest generated TU compile only | `0.507s + 1.5916ms/case * N` |
| GoogleTest TU compile only | `-0.432s + 19.5589ms/case * N` |
| doctest TU compile only | `-0.146s + 6.0387ms/case * N` |

The negative intercepts for the header-only/framework TU fits are an extrapolation artifact from fitting a single line across small and large `N`.
Use the slopes and sampled points as the meaningful signal.

Observed crossovers:

- gentest vs GoogleTest crosses between 25 and 50 cases. Local interpolation puts it near 40 cases; the whole-range fit predicts about 70 cases.
- gentest vs doctest crosses between 250 and 500 cases. Local interpolation puts it near 287 cases; the whole-range fit predicts about 229 cases.

## Results: 8 TUs In One Target Binary

Median wall times in seconds for one binary with up to 8 test TUs. The `codegen` and `gentest TU` columns are median Ninja edge sums, not wall
sub-intervals. They can exceed wall time because compile edges run in parallel.

| Cases | Shards | gentest | codegen | gentest TU | GoogleTest | doctest | d/gtest | d/doctest |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1 | 0.859 | 0.317 | 0.487 | 0.534 | 0.106 | +0.325 | +0.754 |
| 10 | 8 | 1.290 | 0.689 | 4.300 | 0.622 | 0.115 | +0.668 | +1.176 |
| 25 | 8 | 1.327 | 0.692 | 4.599 | 0.642 | 0.122 | +0.685 | +1.205 |
| 50 | 8 | 1.333 | 0.695 | 4.635 | 0.686 | 0.138 | +0.647 | +1.195 |
| 100 | 8 | 1.350 | 0.694 | 4.701 | 0.749 | 0.164 | +0.601 | +1.186 |
| 250 | 8 | 1.383 | 0.702 | 4.966 | 1.016 | 0.249 | +0.367 | +1.135 |
| 500 | 8 | 1.448 | 0.711 | 5.430 | 1.553 | 0.404 | -0.105 | +1.043 |
| 1000 | 8 | 1.593 | 0.742 | 6.313 | 2.708 | 0.785 | -1.116 | +0.808 |
| 2000 | 8 | 1.868 | 0.789 | 8.057 | 5.436 | 1.583 | -3.568 | +0.285 |

Linear fit across all sampled points:

| Path | Fit |
| --- | ---: |
| gentest e2e | `1.238s + 0.3333ms/case * N` |
| GoogleTest e2e | `0.501s + 2.3980ms/case * N` |
| doctest e2e | `0.087s + 0.7322ms/case * N` |

Observed/predicted crossovers:

- gentest vs GoogleTest crosses between 250 and 500 cases; the whole-range fit predicts about 357 cases.
- gentest stayed slower than doctest through 2000 cases; the whole-range fit predicts about 2885 cases.

## Results: 8 Target Binaries

Median wall times in seconds for up to 8 target binaries, one test TU per binary. The `codegen` and `gentest TU` columns are median Ninja edge sums,
not wall sub-intervals. In this layout, gentest runs one codegen process per binary and Ninja schedules those processes concurrently.

| Cases | Shards | gentest | codegen | gentest TU | GoogleTest | doctest | d/gtest | d/doctest |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1 | 0.851 | 0.315 | 0.487 | 0.533 | 0.104 | +0.318 | +0.747 |
| 10 | 8 | 0.966 | 2.837 | 4.332 | 0.622 | 0.122 | +0.344 | +0.844 |
| 25 | 8 | 0.989 | 2.808 | 4.582 | 0.646 | 0.131 | +0.342 | +0.858 |
| 50 | 8 | 0.996 | 2.798 | 4.617 | 0.681 | 0.144 | +0.315 | +0.852 |
| 100 | 8 | 1.014 | 2.850 | 4.738 | 0.737 | 0.170 | +0.277 | +0.844 |
| 250 | 8 | 1.048 | 2.831 | 4.997 | 0.994 | 0.253 | +0.054 | +0.795 |
| 500 | 8 | 1.099 | 2.840 | 5.432 | 1.499 | 0.412 | -0.400 | +0.687 |
| 1000 | 8 | 1.212 | 2.882 | 6.294 | 2.576 | 0.791 | -1.363 | +0.421 |
| 2000 | 8 | 1.455 | 2.986 | 8.059 | 5.134 | 1.583 | -3.680 | -0.128 |

Linear fit across all sampled points:

| Path | Fit |
| --- | ---: |
| gentest e2e | `0.960s + 0.2519ms/case * N` |
| GoogleTest e2e | `0.508s + 2.2482ms/case * N` |
| doctest e2e | `0.093s + 0.7304ms/case * N` |

Observed/predicted crossovers:

- gentest vs GoogleTest crosses between 250 and 500 cases; the whole-range fit predicts about 226 cases.
- gentest vs doctest crosses between 1000 and 2000 cases; the whole-range fit predicts about 1812 cases.

## Results: Fixed Test Workload

Median wall times in seconds for the 26-test repo-derived workload.

| Framework | Median | Mean | Samples |
| --- | ---: | ---: | --- |
| gentest | 1.657 | 1.657 | 1.662, 1.654, 1.655, 1.659, 1.657 |
| GoogleTest | 1.309 | 1.309 | 1.308, 1.309, 1.309, 1.307, 1.313 |
| doctest | 0.554 | 0.554 | 0.550, 0.553, 0.557, 0.555, 0.554 |

Delta:

- gentest minus GoogleTest: `+0.348s` (`+26.58%`)
- gentest minus doctest: `+1.103s` (`+199.30%`)

Last-sample Ninja edge profile:

| Framework | Codegen | TU compile edge sum | Link | Notes |
| --- | ---: | ---: | ---: | --- |
| gentest | 0.623 | 2.095 | 0.037 | 3 generated compile edges; edge sum is not wall time under parallel build |
| GoogleTest | n/a | 2.472 | 0.037 | 3 compile edges |
| doctest | n/a | 0.898 | 0.035 | 3 compile edges |

## Results: Fixed Benchmark Workload

Median wall times in seconds for append-iota benchmark binaries over `std::vector`, `std::list`, and `std::deque`, each with `1,000,000` elements.

| Framework | Median | Mean | Samples |
| --- | ---: | ---: | --- |
| gentest native bench | 0.670 | 0.671 | 0.675, 0.667, 0.677, 0.667, 0.670 |
| Google Benchmark | 0.512 | 0.514 | 0.511, 0.512, 0.516, 0.511, 0.520 |
| nanobench | 0.598 | 0.595 | 0.588, 0.600, 0.598, 0.589, 0.598 |

Delta:

- gentest native bench minus Google Benchmark: `+0.158s` (`+30.78%`)
- gentest native bench minus nanobench: `+0.072s` (`+11.98%`)

Last-sample Ninja edge profile:

| Framework | Codegen | TU compile | Link |
| --- | ---: | ---: | ---: |
| gentest native bench | 0.171 | 0.447 | 0.037 |
| Google Benchmark | n/a | 0.473 | 0.041 |
| nanobench | n/a | 0.561 | 0.031 |

For this tiny benchmark workload, gentest's generated benchmark TU is lighter than the nanobench TU and slightly lighter than the Google Benchmark
TU. The e2e consumer build is still slower because the fixed codegen cost is larger than the compile-time savings for only three benchmarks.

## Findings

1. Codegen scaling matches `O(N) + C` for this workload. The measured fixed cost is roughly `0.29s`, and the fitted slope is about
   `0.273ms/case`. For small test counts, the fixed parse/setup cost dominates. At 2000 tests, codegen was `0.891s`, while generated TU
   compilation was `3.719s`.

2. Manifest validation and generated JSON bookkeeping are not the bottleneck in these runs. The manifest-validation edge was about `0.009s`
   across the scale points.

3. gentest overtakes GoogleTest quickly for synthetic one-TU trivial tests because GoogleTest's macro/header instantiation slope is much steeper.
   The observed table crosses between 25 and 50 tests.

4. doctest is much cheaper than gentest for small test counts, but its per-case compile slope is still higher than gentest's generated-TU slope.
   In this run, gentest overtook doctest between 250 and 500 tests.

5. Splitting one target over 8 TUs changes the wall-time shape. Parallel compile edges reduce GoogleTest and doctest wall time substantially, and
   gentest's fixed codegen cost is less amortized at low counts. In this layout, gentest overtook GoogleTest around the 250-500 case region and
   did not overtake doctest by 2000 cases.

6. Splitting over 8 binaries lets Ninja run independent gentest codegen invocations in parallel. The codegen edge sum is around `2.8-3.0s`, but
   that work is parallelized; gentest overtook GoogleTest around the 250-500 case region and overtook doctest between 1000 and 2000 cases.

7. For fixed benchmark binaries, gentest's generated benchmark TU is not the heavy part. The fixed codegen step is the overhead that must be
   amortized by larger benchmark counts or more expensive framework headers.

## Threats To Validity

- Dirty worktree: rerun from a clean commit for publication-quality numbers.
- Synthetic scale workload: all cases are trivial tests in one TU. Real-world test bodies, fixtures, mocks, and multiple TUs can shift both fixed
  costs and slopes.
- Single host/toolchain: these numbers are from one Linux GCC Release setup. Clang, MSVC, modules, and Debug builds need separate runs.
- Parallel Ninja edge sums: compile edge sums show where work happened, but they are not wall time when multiple compile edges run concurrently.
- Runtime benchmark smoke is not a stable runtime-performance study. The benchmark binaries were primarily included to measure consumer compile
  cost, not benchmark-framework measurement quality.
- The jkammerland/doctest fork currently needs a local `<tuple>` compatibility include in this harness because the pinned revision uses
  `std::tuple_cat` without including `<tuple>`.

## Source Files

- Scale script: [`scripts/bench_case_scale.py`](../scripts/bench_case_scale.py)
- Layout scale script: [`scripts/bench_case_layout_scale.py`](../scripts/bench_case_layout_scale.py)
- Fixed workload script: [`scripts/bench_gtest_compare.py`](../scripts/bench_gtest_compare.py)
- Fixed workload harness: [`benchmarks/gtest_compare/CMakeLists.txt`](../benchmarks/gtest_compare/CMakeLists.txt)
