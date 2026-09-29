# CI test profiles

CI starts on demand for pull requests. Opening, updating, or retargeting a PR
starts no build or test matrix. Pushes to `master` start one full validation
bundle after merge.

## Start CI

After the workflow is merged into `master`, open **Actions → Run CI → Run
workflow**, select the PR or integration branch, and choose a profile. The
native button lives in Actions, rather than the PR conversation. A PR
conversation can link to [Run CI](https://github.com/jkammerland/gentest/actions/workflows/ci.yml).
See [GitHub's manual workflow documentation](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow).

The equivalent CLI command starts the entire bundle:

```bash
gh workflow run ci.yml --ref ci/review-integration-20260926 \
  -f profile=full -f base_ref=master
```

For any PR, resolve its branch and comparison base from its number:

```bash
pr_number=154
ci_branch=$(gh pr view "$pr_number" --json headRefName --jq .headRefName)
ci_base=$(gh pr view "$pr_number" --json baseRefOid --jq .baseRefOid)
gh workflow run ci.yml --ref "$ci_branch" -f profile=full -f base_ref="$ci_base"
```

The bundle calls CMake/platform/package tests, lint, coverage, cross/QEMU,
Bazel/Meson/Xmake, measured comparisons, and recording/serializer checks. All
called workflow files and checkouts use the caller's commit. Each suite also
retains its own manual entry point for a focused rerun, for example
`gh workflow run lint.yml --ref "$ci_branch"`.

Rebase or merge the current target into the candidate before validation: a
manual run tests the selected branch commit, not GitHub's synthetic PR merge.
A later code change or rebase needs a new run. Prefer validating the final
integration branch once after preparing a stack.

## Profiles

`full` is the default. Every CMake matrix job runs its complete configured
CTest inventory, and the package job uses the complete `package` workflow.

`pr` runs the complete inventory on the designated exhaustive Linux lanes.
Other CMake matrix jobs build the same authored targets but run tests carrying
the `ci-compat` label. That label covers the main test executables, public API
checks, representative textual and named-module registration, explicit mocks,
runtime shared-library exports, and module-flag regressions. The package job
uses `package-pr`, which tests installed-consumer contracts without repeating
the complete runtime and nested-helper inventory. Both profiles still run all
seven validation suites.

Expensive installed-package consumers are not duplicated in every toolchain
lane. The dedicated Linux Clang package job owns the complete package
contract, with representative Linux GCC, macOS LLVM, Windows LLVM, and Windows
MSVC jobs retaining cross-toolchain coverage. Other matrix jobs explicitly
set `GENTEST_ENABLE_PACKAGE_TESTS=OFF`.

Measured comparisons resolve `base_ref` to a commit once, then reuse that
commit for the baseline build. On a `master` push, the baseline is the commit
before the push. For manual runs, it defaults to `master`; supply a commit or
branch when a different baseline is needed.

## Parallelism

Standard local system-Clang presets use four outer CTest jobs and cap nested
helper builds at one job through `GENTEST_HELPER_BUILD_PARALLEL_LEVEL=1`. CI
uses the same nested cap. Xmake helper tests share a CTest resource lock because
their tool-level state is not safe to mutate concurrently.

This design deliberately avoids persistent producer, textual parse, or PCM
caches. Every CI job validates artifacts produced from its exact checkout and
toolchain.

## Validation

Inspect the focused inventory after configuring `debug-system`:

```bash
ctest --test-dir build/debug-system --show-only=json-v1 -L '^ci-compat$'
```

Run the same profile locally with:

```bash
ctest --preset=debug-system --output-on-failure -L '^ci-compat$'
```

Run the complete profile by omitting `-L`.
