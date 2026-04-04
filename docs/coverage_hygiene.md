# Coverage Hygiene

This repo has two repo-owned coverage helpers:

- [`scripts/coverage_hygiene.py`](../scripts/coverage_hygiene.py) for artifact and scope sanity checks
- [`scripts/coverage_report.py`](../scripts/coverage_report.py) for aggregate metrics, threshold gating, and report artifacts

`scripts/coverage_hygiene.py` is not a general coverage-reporting tool. Its job
is to sanity-check the repo's own implementation files under `src/` and
`tools/src/` and answer:

- did this source file appear in the build's compilation database
- did it produce usable coverage artifacts
- did it get executable hits
- is it intentionally exempt from the coverage gate

The repo-specific policy for that check lives in
[`scripts/coverage_hygiene.toml`](../scripts/coverage_hygiene.toml).

`scripts/coverage_report.py` reads the same policy file, generates scoped line /
function / branch coverage summaries, writes a Markdown report for the GitHub
job summary, and emits a per-file HTML report artifact.

## What It Checks

The script uses the active build tree to:

1. read `compile_commands.json`
2. find tracked source files under the configured roots
3. map each source to object files and candidate `.gcda` files
4. run a `gcov`-compatible tool on those coverage artifacts
5. classify each file into a repo-specific status

Common statuses include:

- `ok`: coverage data was found and the file had hits
- `zero_hits`: the file had executable lines but no hits
- `missing_obj`: the source did not appear in the compilation database
- `missing_gcda`: the source compiled but no coverage data file was found
- `stamp_mismatch`: stale coverage data did not match the notes file
- `no_match`: a coverage tool ran but could not match the output back to the source
- `gcov_error`: the coverage tool failed in a way that prevented classification
- `intentional`: the file is deliberately treated as exempt by repo policy

## Policy File

[`scripts/coverage_hygiene.toml`](../scripts/coverage_hygiene.toml) is the
repo policy layer.

It defines:

- `roots`: source trees to inspect
- `exclude_prefix`: source subtrees to skip
- `intentional`: files that are intentionally exempt
- `no_exec`: files expected to have no executable lines
- `fail_on`: statuses that should fail the script
- `warn_on`: statuses that should be reported as actionable but not fail
- `[report]`: aggregate coverage thresholds and report-generation policy

Change the TOML when you are updating repo policy.
Change the Python scripts when you are changing collection, classification, or
report-generation logic.

## Local Use

Typical local usage:

```bash
python3 -m pip install --upgrade gcovr
cmake --preset=coverage-system -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build --preset=coverage-system
ctest --preset=coverage-system --output-on-failure --parallel 1
python3 scripts/coverage_hygiene.py \
  --build-dir build/coverage-system \
  --ignore-statuses stamp_mismatch \
  --gcov llvm-cov gcov
python3 scripts/coverage_report.py --build-dir build/coverage-system
```

Notes:

- `--build-dir` points at the coverage-instrumented build tree.
- `--ignore-statuses stamp_mismatch` matches the current CI behavior.
- `scripts/coverage_report.py` auto-selects `llvm-cov gcov` for Clang builds and `gcov` for GCC builds.
- The report script writes `summary.md`, `summary.json`, and `index.html` under `build/<preset>/coverage-report/`.
- If you need a non-default policy file, pass `--config <path>`.

## How CI Uses It

The Linux coverage lane in
[`.github/workflows/cmake.yml`](../.github/workflows/cmake.yml)
does seven things:

1. configures and builds the `coverage-system` preset with Clang
2. deletes old `.gcda` files from `build/coverage-system`
3. runs `ctest` serially for that lane
4. runs `scripts/coverage_hygiene.py`
5. runs `scripts/coverage_report.py`
6. appends `coverage-report/summary.md` to the GitHub job summary
7. uploads the HTML detail report as a workflow artifact

If either script exits non-zero, the coverage CI job fails.

That makes the hygiene script a coverage sanity gate and the report script the
aggregate threshold gate.

## When To Update It

Update the TOML if:

- a new repo-owned source tree should be covered
- a file is intentionally exempt
- the fail/warn policy changes

Update the Python script if:

- coverage artifact discovery must change
- status classification is wrong
- a new coverage-tool behavior must be supported

Update the report script if:

- aggregate threshold logic changes
- the GitHub job summary format changes
- the HTML/JSON artifact layout changes

Update the workflow if:

- the coverage build directory changes
- the chosen `gcov` tool override changes
- the CI policy for ignored statuses changes
- the GitHub job summary or artifact publishing steps change

## Important Constraint

The script is repo-specific. It assumes:

- a CMake-generated `compile_commands.json`
- GCC/gcov-style coverage artifacts
- repo policy encoded in `coverage_hygiene.toml`

If the build layout or coverage toolchain changes substantially, revisit both
the script and the policy file together.
