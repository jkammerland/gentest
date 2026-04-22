# Parallel `gentest_codegen` (TU wrapper mode)

This note documents how `gentest_codegen` parallelizes parsing/emission in **per‑TU wrapper mode** (CMake’s default mode), and how to verify that the parallel output remains identical to serial output.

## Terminology

- **Invocation**: a single `gentest_codegen ...` process started by the build system.
- **Input wrapper TUs**: the generated `tu_*.gentest.cpp` shim sources passed to a single invocation (usually 1 per test target, but can be many).
- **Codegen job**: a **worker thread inside one `gentest_codegen` process**, used to parse/emit multiple wrapper TUs concurrently.

Important: this is independent of `cmake --build -j` / Ninja scheduling, which controls how many *processes / build edges* run in parallel.

## Controls

Parallelism is controlled by:

- `gentest_codegen --jobs=<N>` (0 = auto)
- `GENTEST_CODEGEN_JOBS=<N>` (same semantics; convenient for benchmarks/scripts)

Precedence:

- If `--jobs` is passed (including `--jobs=0`), it overrides `GENTEST_CODEGEN_JOBS`.
- If `--jobs` is not passed, `GENTEST_CODEGEN_JOBS` (when set) supplies the default.

Auto mode (`0`) uses `std::thread::hardware_concurrency()` (clamped to the number of wrapper TUs, with a fallback to 1 if the runtime reports 0).

`GENTEST_CODEGEN_JOBS` also accepts `auto` (case-insensitive) as a synonym for `0`.

## When parallelism is used

Parallel parsing/emission is enabled only when:

- TU wrapper mode is active (`--tu-out-dir` is used), **and**
- the invocation has **more than one** input wrapper TU, **and**
- resolved `jobs > 1`.

## High-level flow (one invocation)

```
Ninja / CMake target build
  |
  +-- runs: gentest_codegen  (1 process)
       inputs: [tu_0000.gentest.cpp, tu_0001.gentest.cpp, ...]
       jobs:   K   (from --jobs or GENTEST_CODEGEN_JOBS; 0=auto)

       PARSE PHASE  (parallel when K>1 and inputs>1)
         warm-up: parse TU[0] serially (initializes LLVM/Clang singletons)
         tasks = indices 1..N-1 for the remaining input TU list
         shared atomic "next_index" over the remaining indices
         K worker threads:
            loop:
              idx = next_index++   (maps to TU[1 + idx])
              if idx >= N-1: exit
              parse TU[1 + idx] with its own clang-tooling objects
              store ParseResult[1 + idx]

       MERGE PHASE  (single thread)
         concatenate all ParseResult[*] cases/mocks
         enforce cross-TU name uniqueness
         sort cases

       EMIT PHASE (per-TU headers, parallel when K>1 and inputs>1)
         tasks = indices 0..N-1 for the same TU list
         shared atomic "next_index"
         K worker threads:
            loop:
              idx = next_index++
              if idx >= N: exit
              render + write tu_XXXX_*.gentest.h

       MOCK OUTPUTS (single thread)
         render + write <target>_mock_registry.hpp + <target>_mock_impl.hpp
```

So yes: wrapper TUs are effectively “pushed to a queue”, implemented as an atomic task index that workers consume from (no explicit `std::queue`).

## Implementation notes (correctness + determinism)

- **Thread isolation during parsing**
  - Each worker creates its own `clang::tooling::ClangTool`, `MatchFinder`, and collectors, and writes only to its per-index `ParseResult`.
  - Results are merged after all workers join.

- **Warm-up parse to avoid TSAN first-use races**
  - When `jobs > 1`, `gentest_codegen` parses one TU serially before spawning worker threads.
  - Some system LLVM/Clang builds can report TSAN data races during first-use singleton initialization when many TUs start parsing concurrently.

- **Deterministic output**
  - After merge, cases are sorted by `display_name` before emission.
  - Cross‑TU uniqueness checking is done on `base_name` (`suite/name`) to preserve prior “duplicate test name” behavior when parsing is parallel.

- **Mock headers during codegen**
  - During codegen (`-DGENTEST_CODEGEN=1`), `gentest/mock.h` skips including the generated mock registry/impl headers.
  - This prevents re-parsing stale/partial mock outputs when codegen is parsing multiple wrapper TUs.

## Verification tooling

### 1) Multi‑TU codegen bench target

The repo defines a **codegen-only** benchmark target that invokes `gentest_codegen` once with many wrapper TUs:

- `gentest_codegen_parallel_bench` (custom target)
- backed by: `gentest_codegen_parallel_bench_obj` (object library)

Build it:

```bash
cmake --preset=debug-system --fresh
cmake --build --preset=debug-system --target gentest_codegen_parallel_bench --clean-first -j 16
```

### 2) Equivalence check: serial vs parallel outputs

Use `scripts/verify_codegen_parallel.py` to hash all generated outputs and compare:

```bash
python3 scripts/verify_codegen_parallel.py \
  --build-dir build/debug-system \
  --target gentest_codegen_parallel_bench_obj \
  --serial-jobs 1 \
  --parallel-jobs 0 \
  --repeats 5
```

### 3) Timing different `GENTEST_CODEGEN_JOBS` caps

`scripts/bench_compile.py` can run the exact codegen command from `build.ninja` with a chosen cap:

```bash
python3 scripts/bench_compile.py \
  --build-dir build/debug-system \
  --no-clean \
  --codegen-only \
  --targets gentest_codegen_parallel_bench_obj \
  --codegen-jobs 4
```

### 4) Representative local speedup

This run isolates one `gentest_codegen` invocation over the `gentest_codegen_parallel_bench_obj` target. In this configuration, the invocation sees
12 wrapper TUs in one process. The generator itself was built with the `release-system` preset so the measurement reflects a Release host tool.

Setup and correctness check:

```bash
cmake --preset=release-system -DGENTEST_ENABLE_PACKAGE_TESTS=OFF
cmake --build --preset=release-system --target gentest_codegen_parallel_bench --clean-first -j 8
python3 scripts/verify_codegen_parallel.py \
  --build-dir build/release-system \
  --target gentest_codegen_parallel_bench_obj \
  --serial-jobs 1 \
  --parallel-jobs 0 \
  --repeats 3
```

Timing command shape:

```bash
python3 scripts/bench_compile.py \
  --preset release-system \
  --jobs 8 \
  --no-clean \
  --codegen-only \
  --targets gentest_codegen_parallel_bench_obj \
  --codegen-jobs <N>
```

Representative local samples, five measured runs per cap after one warm-up run:

| `GENTEST_CODEGEN_JOBS` | Median codegen | Mean | Speedup vs `1` | Samples |
| ---: | ---: | ---: | ---: | --- |
| 1 | 3.353s | 3.357s | 1.00x | 3.353, 3.352, 3.361, 3.346, 3.371 |
| 2 | 2.035s | 2.035s | 1.65x | 2.042, 2.052, 2.035, 2.035, 2.013 |
| 4 | 1.404s | 1.402s | 2.39x | 1.404, 1.399, 1.397, 1.405, 1.404 |
| 8 | 1.125s | 1.124s | 2.98x | 1.128, 1.125, 1.118, 1.119, 1.130 |
| 0 (auto) | 0.914s | 0.913s | 3.67x | 0.912, 0.903, 0.918, 0.916, 0.914 |

The equivalence check passed for serial versus auto parallel output. This is an isolated codegen-process speedup, not a whole-target compile
speedup: final target wall time also includes generated TU compilation, linking, Ninja scheduling, and any other codegen processes running in the
same build.

## Limitations / expectations

- If a given test target has only **one** test TU, a single `gentest_codegen` invocation won’t benefit from internal parallelism.
  - The speedups show up when a single invocation sees many wrapper TUs (e.g., the bench target, or consumer projects with multiple test TUs per target).
- Internal codegen job parallelism can interact with build-system parallelism:
  - `cmake --build -j X` can run multiple `gentest_codegen` processes in parallel;
  - each process can itself use up to `--jobs` threads.
  - Use `--jobs` / `GENTEST_CODEGEN_JOBS` to avoid oversubscription on big builds.
