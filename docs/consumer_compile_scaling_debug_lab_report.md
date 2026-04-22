# Consumer Compile Scaling Debug Lab Report

Date: 2026-04-22

Branch: `fix/ct-benchmark-logs`

Measured commit: `e5b44a2fbbc80aeed45988385319ffadc92fffe0`

Status caveat: these are local lab measurements for Debug consumer builds on this machine, not a final release benchmark. Rerun the commands in this
document on the target machine and toolchain before publishing numbers externally.

Important setup detail: Debug consumers were measured with a Release-built `gentest_codegen` host tool. That matches the intended host-tool model:
codegen build time is excluded, but codegen execution time is included in gentest's measured consumer build path. An all-Debug host tool would make
gentest codegen execution look artificially worse.

Related report: the Release-style lab report is
[`docs/consumer_compile_scaling_lab_report.md`](consumer_compile_scaling_lab_report.md).

## Goal

Measure consumer end-to-end build cost for the same compile-time benchmark harnesses in Debug consumer builds:

- gentest
- GoogleTest
- jkammerland/doctest
- Google Benchmark, for benchmark comparison
- nanobench, for benchmark comparison

The timed section intentionally excludes dependency fetch/build time, the `gentest_codegen` tool build, gentest runtime build time, and helper
library builds. For gentest, the timed section includes consumer codegen execution, generated TU compilation, and final consumer binary link.

## Environment

Representative local run:

- OS: Linux
- CMake: `4.3.1`
- Ninja: `1.14.0.git`
- Compiler: `c++ (GCC) 15.2.1 20260123 (Red Hat 15.2.1-7)`
- Consumer build type: `Debug`
- `gentest_codegen` host-tool build type: `Release`
- Build jobs: `8`
- Samples: `3` for scaling runs, `5` for fixed workload runs
- ccache: disabled by script default via `CCACHE_DISABLE=1`
- GoogleTest: `1.15.2`, from `pkg-config`
- doctest: `https://github.com/jkammerland/doctest.git @ 397ce0086a7ba36524c5706dfc2820527260e65b`
- Google Benchmark: `https://github.com/google/benchmark.git @ 192ef10025eb2c4cdd392bc502f0c852196baa48`
- nanobench: `https://github.com/martinus/nanobench.git @ e4327893194f06928012eb81cabc606c4e4791ac`

The scripts clear `CMAKE_TOOLCHAIN_FILE` so the vcpkg preset path does not affect these harnesses. Set `CC` and `CXX` in the environment before
running the scripts if you need to pin a different compiler.

## Reproduction

The scripts write JSON results under `build/` and print a Markdown-friendly summary to stdout.

### Scaling: one TU

```bash
rm -rf build/bench-case-scale
python3 scripts/bench_case_scale.py \
  --counts 1,10,25,50,100,250,500,1000,2000 \
  --samples 3 \
  --jobs 8 \
  --build-type Debug \
  --codegen-build-type Release \
  --skip-run \
  --output build/bench-case-scale/case_scale_debug_release_codegen.json
```

### Scaling: up to 8 TUs in one binary

```bash
rm -rf build/bench-case-layout-scale/multi-tu-shards-8
python3 scripts/bench_case_layout_scale.py \
  --layout multi-tu \
  --shards 8 \
  --counts 1,10,25,50,100,250,500,1000,2000 \
  --samples 3 \
  --jobs 8 \
  --build-type Debug \
  --codegen-build-type Release \
  --skip-run \
  --output build/bench-case-layout-scale/multi-tu-shards-8/case_scale_multi-tu_shards_8_debug_release_codegen.json
```

### Scaling: up to 8 target binaries

```bash
rm -rf build/bench-case-layout-scale/multi-binary-shards-8
python3 scripts/bench_case_layout_scale.py \
  --layout multi-binary \
  --shards 8 \
  --counts 1,10,25,50,100,250,500,1000,2000 \
  --samples 3 \
  --jobs 8 \
  --build-type Debug \
  --codegen-build-type Release \
  --skip-run \
  --output build/bench-case-layout-scale/multi-binary-shards-8/case_scale_multi-binary_shards_8_debug_release_codegen.json
```

### Fixed repo-derived workload

```bash
rm -rf build/bench-gtest-compare
python3 scripts/bench_gtest_compare.py \
  --samples 5 \
  --jobs 8 \
  --build-type Debug \
  --codegen-build-type Release \
  --output build/bench-gtest-compare/gtest_compare_debug_release_codegen.json
```

## Results: Test Count Scaling

Median wall times in seconds. `d/gtest` and `d/doctest` are `gentest - other`; negative means gentest is faster. The `codegen` and `gentest TU`
columns are Ninja edge summaries, not the full gentest wall time.

| Cases | gentest | codegen | gentest TU | GoogleTest | doctest | d/gtest | d/doctest |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 0.916 | 0.325 | 0.481 | 0.572 | 0.119 | +0.344 | +0.797 |
| 10 | 0.926 | 0.329 | 0.485 | 0.607 | 0.127 | +0.319 | +0.799 |
| 25 | 0.946 | 0.332 | 0.499 | 0.670 | 0.149 | +0.276 | +0.797 |
| 50 | 0.963 | 0.338 | 0.513 | 0.764 | 0.178 | +0.199 | +0.785 |
| 100 | 0.997 | 0.344 | 0.543 | 0.967 | 0.234 | +0.030 | +0.763 |
| 250 | 1.095 | 0.350 | 0.632 | 1.605 | 0.416 | -0.511 | +0.679 |
| 500 | 1.266 | 0.377 | 0.773 | 2.816 | 0.741 | -1.551 | +0.524 |
| 1000 | 1.682 | 0.492 | 1.073 | 5.529 | 1.440 | -3.847 | +0.242 |
| 2000 | 2.803 | 0.900 | 1.778 | 11.222 | 2.940 | -8.418 | -0.137 |

Linear fit across all sampled points:

| Path | Fit |
| --- | ---: |
| gentest e2e | `0.887s + 0.9174ms/case * N` |
| GoogleTest e2e | `0.431s + 5.3033ms/case * N` |
| doctest e2e | `0.091s + 1.4034ms/case * N` |

Observed/predicted crossovers:

- gentest vs GoogleTest crosses between 100 and 250 cases; the whole-range fit predicts about 104 cases.
- gentest vs doctest crosses between 1000 and 2000 cases; the whole-range fit predicts about 1638 cases.

## Results: 8 TUs In One Target Binary

Median wall times in seconds for one binary with up to 8 test TUs. The `codegen` and `gentest TU` columns are median Ninja edge sums, not wall
sub-intervals. They can exceed wall time because compile edges run in parallel.

| Cases | Shards | gentest | codegen | gentest TU | GoogleTest | doctest | d/gtest | d/doctest |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1 | 0.939 | 0.336 | 0.486 | 0.587 | 0.120 | +0.352 | +0.819 |
| 10 | 8 | 1.393 | 0.724 | 4.258 | 0.659 | 0.127 | +0.734 | +1.266 |
| 25 | 8 | 1.378 | 0.715 | 4.272 | 0.668 | 0.126 | +0.710 | +1.251 |
| 50 | 8 | 1.385 | 0.717 | 4.277 | 0.703 | 0.130 | +0.682 | +1.255 |
| 100 | 8 | 1.401 | 0.723 | 4.331 | 0.732 | 0.139 | +0.669 | +1.262 |
| 250 | 8 | 1.411 | 0.722 | 4.460 | 0.830 | 0.163 | +0.581 | +1.248 |
| 500 | 8 | 1.452 | 0.746 | 4.509 | 1.007 | 0.205 | +0.445 | +1.247 |
| 1000 | 8 | 1.521 | 0.766 | 4.937 | 1.424 | 0.300 | +0.097 | +1.220 |
| 2000 | 8 | 1.659 | 0.823 | 5.518 | 2.288 | 0.490 | -0.629 | +1.170 |

Linear fit across all sampled points:

| Path | Fit |
| --- | ---: |
| gentest e2e | `1.309s + 0.1923ms/case * N` |
| GoogleTest e2e | `0.630s + 0.8191ms/case * N` |
| doctest e2e | `0.120s + 0.1832ms/case * N` |

Observed/predicted crossovers:

- gentest vs GoogleTest crosses between 1000 and 2000 cases; the whole-range fit predicts about 1083 cases.
- gentest stayed slower than doctest through 2000 cases. The whole-range fit does not predict a positive crossover for this layout.

## Isolated Multi-TU Codegen Speedup

The synthetic layout tables above measure whole consumer target wall time. To isolate internal codegen parallelism, the repository also has a
codegen-only benchmark target that invokes one `gentest_codegen` process over 12 wrapper TUs:
`gentest_codegen_parallel_bench_obj`.

Representative local Release-host-tool run:

| `GENTEST_CODEGEN_JOBS` | Median codegen | Mean | Speedup vs `1` |
| ---: | ---: | ---: | ---: |
| 1 | 3.353s | 3.357s | 1.00x |
| 2 | 2.035s | 2.035s | 1.65x |
| 4 | 1.404s | 1.402s | 2.39x |
| 8 | 1.125s | 1.124s | 2.98x |
| 0 (auto) | 0.914s | 0.913s | 3.67x |

Serial and auto-parallel output hashes matched in `scripts/verify_codegen_parallel.py` with 3 repeated parallel runs. See
[`docs/stories/005_codegen_parallelism_jobs.md`](stories/005_codegen_parallelism_jobs.md) for the exact commands and samples.

## Results: 8 Target Binaries

Median wall times in seconds for up to 8 target binaries, one test TU per binary. The `codegen` and `gentest TU` columns are median Ninja edge sums,
not wall sub-intervals. In this layout, gentest runs one codegen process per binary and Ninja schedules those processes concurrently.

| Cases | Shards | gentest | codegen | gentest TU | GoogleTest | doctest | d/gtest | d/doctest |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1 | 0.917 | 0.327 | 0.475 | 0.566 | 0.115 | +0.350 | +0.802 |
| 10 | 8 | 1.016 | 2.825 | 4.207 | 0.647 | 0.139 | +0.369 | +0.878 |
| 25 | 8 | 1.039 | 2.886 | 4.292 | 0.669 | 0.141 | +0.371 | +0.898 |
| 50 | 8 | 1.049 | 2.890 | 4.293 | 0.681 | 0.146 | +0.369 | +0.903 |
| 100 | 8 | 1.065 | 2.963 | 4.380 | 0.722 | 0.156 | +0.343 | +0.909 |
| 250 | 8 | 1.065 | 2.941 | 4.473 | 0.795 | 0.178 | +0.270 | +0.887 |
| 500 | 8 | 1.088 | 2.958 | 4.604 | 0.945 | 0.224 | +0.142 | +0.864 |
| 1000 | 8 | 1.126 | 2.971 | 4.890 | 1.247 | 0.313 | -0.121 | +0.813 |
| 2000 | 8 | 1.226 | 3.108 | 5.556 | 1.932 | 0.496 | -0.706 | +0.730 |

Linear fit across all sampled points:

| Path | Fit |
| --- | ---: |
| gentest e2e | `1.018s + 0.1079ms/case * N` |
| GoogleTest e2e | `0.630s + 0.6448ms/case * N` |
| doctest e2e | `0.133s + 0.1815ms/case * N` |

Observed/predicted crossovers:

- gentest vs GoogleTest crosses between 500 and 1000 cases; the whole-range fit predicts about 724 cases.
- gentest stayed slower than doctest through 2000 cases; the whole-range fit predicts a much later crossover around 12042 cases.

## Results: Fixed Test Workload

Median wall times in seconds for the 26-test repo-derived workload. The executable lists 3 skipped cases. This is a non-trivial target-level
benchmark, not an isolated framework-overhead benchmark.

| Framework | Median | Mean | Samples |
| --- | ---: | ---: | --- |
| gentest | 1.425 | 1.429 | 1.424, 1.445, 1.421, 1.428, 1.425 |
| GoogleTest | 0.859 | 0.861 | 0.858, 0.864, 0.857, 0.859, 0.865 |
| doctest | 0.415 | 0.416 | 0.415, 0.415, 0.419, 0.415, 0.416 |

Delta:

- gentest minus GoogleTest: `+0.566s` (`+65.87%`)
- gentest minus doctest: `+1.010s` (`+243.32%`)

Last-sample Ninja edge profile:

| Framework | Codegen | TU compile edge sum | Link | Notes |
| --- | ---: | ---: | ---: | --- |
| gentest | 0.637 | 1.705 | 0.105 | 3 generated compile edges; edge sum is not wall time under parallel build |
| GoogleTest | n/a | 2.036 | 0.050 | 3 compile edges |
| doctest | n/a | 0.700 | 0.054 | 3 compile edges |

What this shows:

- GoogleTest's compile edge sum was higher than gentest's generated TU compile edge sum (`2.036s` vs `1.705s`), but GoogleTest still had
  lower end-to-end wall time (`0.859s` vs `1.425s`) because gentest also paid `0.637s` of codegen.
- doctest remained substantially faster on this small fixed Debug test target.
- The fixed workload is useful for observed target cost, but it includes shared test logic, STL headers, helper code, codegen topology, and
  parallel scheduling. It should not be used alone as a framework registration scaling claim.

## Results: Fixed Benchmark Workload

Median wall times in seconds for append-iota benchmark binaries over `std::vector`, `std::list`, and `std::deque`, each with `1,000,000`
elements.

| Framework | Median | Mean | Samples |
| --- | ---: | ---: | --- |
| gentest native bench | 0.781 | 0.781 | 0.787, 0.775, 0.784, 0.781, 0.779 |
| Google Benchmark | 0.605 | 0.604 | 0.608, 0.606, 0.601, 0.601, 0.605 |
| nanobench | 0.661 | 0.667 | 0.661, 0.661, 0.659, 0.680, 0.673 |

Delta:

- gentest native bench minus Google Benchmark: `+0.176s` (`+29.02%`)
- gentest native bench minus nanobench: `+0.120s` (`+18.10%`)

Last-sample Ninja edge profile:

| Framework | Codegen | TU compile | Link |
| --- | ---: | ---: | ---: |
| gentest native bench | 0.178 | 0.488 | 0.098 |
| Google Benchmark | n/a | 0.491 | 0.107 |
| nanobench | n/a | 0.616 | 0.051 |

In this Debug run, the benchmark TU compile spread was `0.128s` (`0.488s` to `0.616s`) across all three frameworks. gentest's benchmark TU was
near Google Benchmark and lighter than nanobench, but the end-to-end target was still slower because the `0.178s` codegen edge sat on the consumer
build path.

## Findings

1. Building `gentest_codegen` as Release materially changes the Debug scaling result. The previous all-Debug host-tool measurement overstated
   codegen execution cost. This report is the corrected Debug-consumer benchmark.

2. Debug one-TU scaling still shows gentest amortizing fixed codegen cost against GoogleTest. The observed table crosses between 100 and 250
   cases, and the whole-range fit predicts about 104 cases.

3. With Release codegen, Debug one-TU scaling also crosses doctest near the high end. The observed table crosses between 1000 and 2000 cases, and
   the whole-range fit predicts about 1638 cases.

4. Splitting one target over 8 TUs pushes crossovers later because GoogleTest and doctest get more parallel compile work. gentest crossed GoogleTest
   between 1000 and 2000 cases and did not cross doctest by 2000 cases.

5. Splitting over 8 target binaries gives gentest more independent codegen and compile work for Ninja to schedule. In this layout, gentest crossed
   GoogleTest between 500 and 1000 cases, but still did not cross doctest through 2000 cases.

6. The fixed repo-derived Debug tests and benchmarks show the same masking pattern as the Release report: generated TU edge sums can look
   competitive while target wall time remains slower because codegen is also on the critical path.

7. Debug numbers should be treated separately from Release numbers. Optimization level, debug-info generation, link behavior, dependency
   compilation cost, and target layout change both fixed costs and slopes.

## Threats To Validity

- Synthetic scale workload: all cases are trivial tests. Real-world fixtures, mocks, assertions, and dependency-heavy bodies can shift both fixed
  costs and slopes.
- Fixed workload: the repo-derived tests and benchmarks intentionally include shared dependency and helper headers, so they are proof of
  target-level build impact, not isolated measurements of framework registration overhead.
- Machine state: these are local wall-time measurements. CPU frequency, background load, filesystem cache state, and installed compiler packages can
  move absolute timings.

## Source Files

- Scale script: [`scripts/bench_case_scale.py`](../scripts/bench_case_scale.py)
- Layout scale script: [`scripts/bench_case_layout_scale.py`](../scripts/bench_case_layout_scale.py)
- Fixed workload script: [`scripts/bench_gtest_compare.py`](../scripts/bench_gtest_compare.py)
- Fixed workload harness: [`benchmarks/gtest_compare/CMakeLists.txt`](../benchmarks/gtest_compare/CMakeLists.txt)
- Append-iota workload: [`benchmarks/gtest_compare/append_iota_workload.hpp`](../benchmarks/gtest_compare/append_iota_workload.hpp)
- gentest benchmark port:
  [`benchmarks/gtest_compare/gentest_append_iota_bench.cpp`](../benchmarks/gtest_compare/gentest_append_iota_bench.cpp)
- Google Benchmark port:
  [`benchmarks/gtest_compare/gbench_append_iota_bench.cpp`](../benchmarks/gtest_compare/gbench_append_iota_bench.cpp)
- nanobench port:
  [`benchmarks/gtest_compare/nanobench_append_iota_bench.cpp`](../benchmarks/gtest_compare/nanobench_append_iota_bench.cpp)
