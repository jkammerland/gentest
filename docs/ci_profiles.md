# Continuous integration

Lint runs automatically on every pull request and on every push to `master`.
Full CI, which builds and tests gentest on every supported platform and
toolchain, runs only when a maintainer requests it. This page shows how to
request full CI, what it runs, and how requests are checked. Full CI results
are informational: they don't change branch protection or merge requirements.

## What runs automatically

The `lint` workflow (`.github/workflows/lint.yml`) runs Clang Format
(`scripts/check_clang_format.sh`) and Clang Tidy (`scripts/check_clang_tidy.sh`
on a `debug-system` build tree) with the LLVM 23 toolchain from
`scripts/llvm_toolchain.json`. It runs on pushes to `master` and when a pull
request is opened, updated, reopened or marked ready for review. Drafts and
documentation-only changes are included; there is no path filter.
[CONTRIBUTING.md](../CONTRIBUTING.md) shows how to run both checks locally.

Apart from the two workflows that handle full-CI requests (described next),
no other workflow starts on its own.

## Request full CI for a pull request

A bot comment on each pull request shows the current head and base commits and
a checkbox labelled "Run full CI for this revision". To start full CI, a
maintainer either:

- ticks that checkbox, or
- posts a comment whose whole text is `/full-ci`.

Only users with `write`, `maintain` or `admin` permission on the repository can
start a run; author association doesn't count. Edits by bots and edits to other
comments are ignored. If ticking the box doesn't start a run, post `/full-ci`.

Full CI tests GitHub's merge commit of the pull request into its base, so pull
requests from forks work the same way. The controller freezes the head, base
and merge commits when it accepts a request. It refuses the request when:

- the pull request has conflicts, or GitHub hasn't computed a conflict-free
  merge commit yet. The bot comment says so; retry `/full-ci` later.
- the checkbox belongs to an older revision. Every push resets the bot comment
  and asks you to request again, so tick the box again.
- the head or base changes while the request is being checked.

The summary of the "Requested full CI" workflow run states the reason for each
refusal.

A request for a revision whose full CI is already running links to that run
instead of starting another. Full CI for a newer revision of the same pull
request cancels the run for the older one; runs for different pull requests
never cancel each other. You can request a finished or cancelled revision
again.

When the run ends, the bot comment shows its result, unless a newer request or
a change to the head or base has replaced it. The final job, "Full
CI result (informational)", fails if any called workflow failed, was skipped
or was cancelled.

## Run full CI on a branch or on master

Start the `CI` workflow by hand to validate a branch, or a commit on `master`:

```bash
gh workflow run ci.yml --ref master -f base_ref=<comparison-base-commit>
```

`base_ref` (default `master`) is the branch, tag or commit that the
measured-report comparison uses as its baseline. The Actions page offers the
same entry point. A run started this way on `master` gets the title
`CI (full) — master`; the release workflow signs and publishes a tag only when
its exact `master` commit has such a successful run, and public releases
publish signed source archives only (see [Packaging](packaging.md)).

Each workflow in the next section (`cmake.yml`, `coverage.yml`,
`cross_qemu.yml` and the others) can also be started on its own for diagnosis.

## What full CI runs

`ci.yml` freezes the candidate and base commits, then calls all of these
workflows. There are no partial profiles and no selection by changed paths:
every full CI run covers the whole set.

| Workflow | What it checks |
| --- | --- |
| `cmake.yml` | The build matrix on Linux, Windows and macOS (below), the package workflow, and an LLVM 23 build with libassert and without exceptions |
| `lint.yml` | Clang Format and Clang Tidy |
| `coverage.yml` | The coverage gate, see [Coverage hygiene and reporting](coverage_hygiene.md) |
| `cross_qemu.yml` | aarch64 and riscv64 cross builds tested under QEMU |
| `buildsystems_linux.yml` | Bazel, Meson and Xmake consumers |
| `measured_reports.yml` | Benchmark and jitter reports compared against the base commit |
| `recording.yml` | Runtime recording with the real Glaze and cbor_tags serializers |
| `clang_leaks.yml` | Two strict expected-failure probes for known libclang leaks in Clang 20 |

### Build matrix

`scripts/ci_matrix.json` lists the rows of `cmake.yml`: 33 Linux rows, three
Windows LLVM versions (each built with Clang and with MSVC) and eight macOS
rows. Before any job starts, `scripts/ci_matrix.py` checks that:

- every row names its host LLVM major, 19 or newer;
- rows whose host LLVM isn't 23 are marked as compatibility rows;
- the primary rows use LLVM 23, and on Windows the pinned 23.1.0 release;
- no two Windows rows build the same configuration (LLVM version, variant and
  C++ standard).

Each job then checks with `scripts/llvm_tools.py check-toolchain` that its host
LLVM tools and CMake packages belong to the row's LLVM major. The host LLVM
doesn't replace the compiler under test: GCC, MSVC, AppleClang and cross
compilers stay separate inputs.

- The primary Linux rows (LLVM 23) run the full Debug CTest inventory,
  ASan/LSan/UBSan (`alusan-system`) and TSan (`tsan-system`). TSan builds run
  the death tests too; their `tsan_death_tests_enabled` check fails if no death
  test is configured or any is disabled.
- Windows rows build only `RelWithDebInfo`, with explicit `--config` and `-C`
  arguments. Every LLVM version runs both Clang and MSVC, and the rows of one
  LLVM version also run the installed-package tests.
- The Allure writer tests are on by default (`GENTEST_ENABLE_ALLURE_TESTS`), so
  every row runs them.
- Every native job runs its complete configured CTest inventory.

### Installed-package tests

Rows that enable `GENTEST_ENABLE_PACKAGE_TESTS` build, install and consume
gentest within one CTest run. CTest fixtures build one installed prefix for
each distinct set of producer inputs (module policy, compiler and toolchain,
build configuration), and the consumer tests with those inputs share it. Every
CTest run rebuilds the producers from source, even with `ctest -R`. Windows
uses `RelWithDebInfo`; Linux and macOS also check exact `MinSizeRel` imports.

### QEMU, coverage and leak probes

- The QEMU jobs build every configured target with the `aarch64-qemu-full` and
  `riscv64-qemu-full` presets and run the complete configured CTest inventory.
  They install Meson, the system fmt headers, Bazelisk and Xmake, so the
  build-system consumer checks run as well; on a local machine without those
  tools, those checks are reported as CTest skips. The `aarch64-qemu` and
  `riscv64-qemu` presets run a smaller smoke set for diagnosis. See
  [Cross-testing gentest under QEMU](cross_compile/linux_qemu.md).
- The coverage job is described in
  [Coverage hygiene and reporting](coverage_hygiene.md#how-ci-runs-it).
- No row runs Clang 20 under AddressSanitizer end to end, because libclang 20
  leaks in two known places. `clang_leaks.yml` instead keeps two isolated
  probes, with leak detection on, that must reproduce exactly those leaks: they
  fail if the leak disappears or anything else goes wrong. See the
  [dependent-diagnostic leak](issues/libclang_dependent_diagnostic_leak.md) and
  the [deduction-failure leak](issues/libclang_deduction_failure_leak.md).

## Trust and permissions

- The pull-request metadata workflows (`full_ci_prompt.yml`,
  `full_ci_request.yml`) run controller code (`scripts/full_ci.py`) from the
  default branch and never run candidate code. A change to the controller in a
  pull request takes effect only after it is merged.
- The `issue_comment` workflow, `full_ci_request.yml`, calls `ci.yml` and,
  through it, the workflows under [What full CI runs](#what-full-ci-runs) as
  local reusable workflows, so their definitions also come from the trusted
  default-branch revision.
- Each called workflow checks out the frozen candidate commit explicitly, with
  `persist-credentials: false`. Matrix selection and LLVM provisioning run from
  the trusted workflow revision before any candidate code.
- Only the metadata jobs can update the pull-request comment. The called
  workflows' jobs have `contents: read`, inherit no secrets, and only restore caches. GitHub's
  low-trust `issue_comment` cache token also restricts writes, and the called
  workflows don't ask for a more privileged cache mode.
- No GitHub App, dispatch token or `actions: write` permission is needed.

See GitHub's
[reusable-workflow permission and concurrency rules](https://docs.github.com/en/actions/reference/workflows-and-actions/reusing-workflow-configurations)
and the
[cache action's restore-only usage](https://github.com/actions/cache/blob/main/restore/README.md).

## Local validation

[Private local validation](local_validation.md) runs the same kinds of checks
on your own Linux, Windows and macOS machines. Its results stay local; nothing
uploads them. Validate again after the head or base changes, because that
changes the merge result.
