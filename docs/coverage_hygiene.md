# Coverage Hygiene

This repo has a dedicated coverage hygiene check in
[`scripts/coverage_hygiene.py`](../scripts/coverage_hygiene.py).

It is not a general coverage-reporting tool. Its job is to sanity-check the
repo's own implementation files under `src/` and `tools/src/` and answer:

- did this source file appear in the build's compilation database
- did it produce usable coverage artifacts
- did it get executable hits
- is it intentionally exempt from the coverage gate

The repo-specific policy for that check lives in
[`scripts/coverage_hygiene.toml`](../scripts/coverage_hygiene.toml).

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

Change the TOML when you are updating repo policy.
Change the Python script when you are changing collection or classification logic.

## Local Use

Typical local usage:

```bash
cmake --preset=coverage
cmake --build --preset=coverage
ctest --preset=coverage --output-on-failure --parallel 1
python3 scripts/coverage_hygiene.py \
  --build-dir build/coverage \
  --ignore-statuses stamp_mismatch \
  --gcov gcov
```

Notes:

- `--build-dir` points at the coverage-instrumented build tree.
- `--ignore-statuses stamp_mismatch` matches the current CI behavior.
- `--gcov gcov` uses the plain `gcov` tool explicitly; this is intentional.
- If you need a non-default policy file, pass `--config <path>`.

## How CI Uses It

The Linux coverage lane in
[`.github/workflows/cmake.yml`](../.github/workflows/cmake.yml)
does four things:

1. configures and builds the `coverage` preset
2. deletes old `.gcda` files from `build/coverage`
3. runs `ctest` serially for that lane
4. runs `scripts/coverage_hygiene.py`

If the script exits non-zero, the coverage CI job fails.

That makes this script a coverage sanity gate, not just an informational report.

## When To Update It

Update the TOML if:

- a new repo-owned source tree should be covered
- a file is intentionally exempt
- the fail/warn policy changes

Update the Python script if:

- coverage artifact discovery must change
- status classification is wrong
- a new coverage-tool behavior must be supported

Update the workflow if:

- the coverage build directory changes
- the chosen `gcov` tool changes
- the CI policy for ignored statuses changes

## Important Constraint

The script is repo-specific. It assumes:

- a CMake-generated `compile_commands.json`
- GCC/gcov-style coverage artifacts
- repo policy encoded in `coverage_hygiene.toml`

If the build layout or coverage toolchain changes substantially, revisit both
the script and the policy file together.
