# Coverage hygiene and reporting

The coverage gate checks that every implementation file of gentest produces
usable coverage data, and that line and branch coverage stay above fixed
thresholds. Two scripts implement it:

- [`scripts/coverage_hygiene.py`](../scripts/coverage_hygiene.py) checks each
  file under `src/` and `tools/src/`: was it compiled, did it produce coverage
  data, and did any of its lines run?
- [`scripts/coverage_report.py`](../scripts/coverage_report.py) measures
  aggregate line, branch and function coverage with gcovr, fails below the
  thresholds, and writes Markdown, JSON and HTML reports.

Both read their policy from
[`scripts/coverage_hygiene.toml`](../scripts/coverage_hygiene.toml). The CI
coverage job fails when either script fails.

## Run the gate

[Coverage in CONTRIBUTING.md](../CONTRIBUTING.md#coverage) has the whole
sequence: install gcovr 8.6, build the `coverage-system` preset, delete old
coverage data, and run CTest serially. Then run the two scripts on the build
tree:

```bash
llvm_cov="$(python3 scripts/llvm_tools.py resolve llvm-cov)"
python3 scripts/coverage_hygiene.py --build-dir build/coverage-system --ignore-statuses stamp_mismatch --gcov "$llvm_cov" gcov
python3 scripts/coverage_report.py --build-dir build/coverage-system --gcov "$llvm_cov" gcov
```

- `--build-dir` is the coverage-instrumented build tree (default
  `build/coverage-system`).
- `--gcov` is the gcov-compatible command. Without it, both scripts use
  `llvm-cov gcov` for Clang builds and `gcov` for GCC builds, taking whichever
  `llvm-cov` or `gcov` is first on `PATH`. Pass the LLVM 23 `llvm-cov` so that
  its version matches the compiler.
- `--ignore-statuses stamp_mismatch` keeps stale-data mismatches from failing
  the hygiene check, as in CI.
- `--config <path>` reads another policy file.

## What the hygiene check reports

For the build tree you give it, `scripts/coverage_hygiene.py`:

1. reads `compile_commands.json`;
2. finds the `.cpp` files under the policy's `roots`, minus `exclude_prefix`;
3. maps each source to its object files and their `.gcda` files;
4. runs the gcov command on them, which also reports the implementation
   headers under the roots that those sources include;
5. gives each file a status.

It prints one table per group (`codegen` for `tools/src/`, `testlib` for
`src/`), counts the statuses, and lists the failures:

| Status | Meaning |
| --- | --- |
| `ok` | Coverage data found, and some lines ran |
| `zero_hits` | The file has executable lines, but none of them ran |
| `no_executable_lines` | gcov found no executable lines in the file |
| `no_exec` | Same, for a file listed in `no_exec` |
| `missing_obj` | No object file: the file has no compile command, or its object path couldn't be resolved |
| `missing_gcda` | The file compiled, but no `.gcda` file was found |
| `stamp_mismatch` | The `.gcda` file is stale and doesn't match the notes file |
| `no_match` | gcov ran, but its output couldn't be matched to the file |
| `gcov_error` | gcov failed in a way that prevents classification |
| `intentional` | The policy exempts the file |

The script exits non-zero when a file has a status listed in `fail_on`, unless
`--ignore-statuses` names it. Statuses in `warn_on` are counted as actionable
in the output without failing the check. The options `--roots`,
`--exclude-prefix`, `--intentional`, `--no-exec`, `--fail-on` and `--warn-on`
override the matching policy entries for one run.

## What the report measures

`scripts/coverage_report.py` runs gcovr over the build tree. Its totals cover
the repository's implementation files under `src/` and `tools/src/`, including
internal headers in those trees, minus the files the policy exempts. It fails
when line or branch coverage is below the thresholds in `[report]` (75% and
45%); function coverage is reported only.

It writes to `<build-dir>/coverage-report/` (change it with `--output-dir`):

- `summary.md`: the totals against the thresholds and the files with the
  lowest branch coverage, for the GitHub job summary;
- `summary.json`: gcovr's JSON summary;
- `index.html` and one page per file: the detailed HTML report.

`--fail-under-line`, `--fail-under-branch` and `--top-files` override the
`[report]` values for one run. `--incomplete` marks the report as diagnostic
only: `summary.md` starts with an INCOMPLETE notice, the HTML title becomes
`INCOMPLETE coverage`, and the script fails even when the thresholds are met.
CI passes it when the tests or the hygiene check failed.

## Policy file

[`scripts/coverage_hygiene.toml`](../scripts/coverage_hygiene.toml) holds the
repository's policy:

| Key | Meaning |
| --- | --- |
| `roots` | Source trees to check (`src`, `tools/src`) |
| `exclude_prefix` | Subtrees to skip |
| `intentional` | Files exempt from the gate with every compiler |
| `[intentional_by_compiler]` | `clang` and `gcc` lists of files that are exempt only when the build used that compiler family, as recorded by CMake |
| `no_exec` | Files expected to have no executable lines |
| `fail_on` | Statuses that fail the hygiene check |
| `warn_on` | Statuses reported as actionable without failing |
| `[report]` | `fail_under_line`, `fail_under_branch`, `top_files` (rows in the lowest-coverage table) and `gcov_ignore_parse_errors` (passed to gcovr) |

For example, `[intentional_by_compiler]` exempts `src/bench_util.cpp` for Clang
and GCC. Those compilers use inline barriers and only native MSVC calls its
opaque fallback, so their static executables don't link its coverage counters.

## How CI runs it

The `coverage` workflow
([`.github/workflows/coverage.yml`](../.github/workflows/coverage.yml)) runs in a
Fedora 43 container with LLVM 23. It:

1. configures and builds the `coverage-system` preset with Clang;
2. deletes old `.gcda` files and any old `coverage-report/` directory;
3. runs `ctest --preset=coverage-system --output-on-failure --parallel 1 --no-tests=error`;
4. runs `scripts/coverage_hygiene.py --ignore-statuses stamp_mismatch --gcov llvm-cov gcov`
   when the tests ran, whether they passed or failed;
5. runs `scripts/coverage_report.py`, with `--incomplete` when the tests or the
   hygiene check failed;
6. appends `coverage-report/summary.md` to the GitHub job summary, after an
   INCOMPLETE notice when the tests or the hygiene check failed;
7. uploads `coverage-report/` as the `coverage-coverage-system` artifact
   whenever a summary exists.

The job fails if either script fails, but the summary and HTML are written
first, so they appear in the job summary and the artifact either way. The
workflow runs as part of full CI, or on its own from the Actions page; see
[Continuous integration](ci_profiles.md).

## When to change what

| Change | Edit |
| --- | --- |
| Source trees to cover, exempt files, the statuses that fail or warn, the thresholds | `scripts/coverage_hygiene.toml` |
| How coverage files are found, how statuses are classified, support for another gcov tool | `scripts/coverage_hygiene.py` |
| Threshold logic, the job summary format, the HTML or JSON layout | `scripts/coverage_report.py` |
| The build directory, the gcov command, the ignored statuses, the summary and artifact steps | `.github/workflows/coverage.yml` |

Both scripts assume gcov-style coverage data from `--coverage` builds; the
hygiene check also needs the CMake-generated `compile_commands.json`.
