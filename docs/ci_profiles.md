# CI profiles and private validation

Every pull request automatically runs the full CI bundle when opened, updated,
reopened, or marked ready for review, including documentation changes and draft
PRs. Every planner lane is enabled for PRs; path classification cannot skip a
suite. Pushes to `master`, the Actions button, and CLI also default to `full`:

```bash
gh workflow run ci.yml --ref <candidate-branch>
```

Use [Actions → CI → Run workflow](https://github.com/jkammerland/gentest/actions/workflows/ci.yml)
to choose a PR branch. The default **profile: full** runs the exhaustive bundle.
For a PR whose branch is in this repository, this command resolves its head
branch and exact base commit automatically:

```bash
pr=177 # Replace with the PR number.
gh workflow run ci.yml --repo jkammerland/gentest \
  --ref "$(gh pr view "$pr" --repo jkammerland/gentest --json headRefName --jq .headRefName)" \
  -f profile=full \
  -f base_ref="$(gh pr view "$pr" --repo jkammerland/gentest --json baseRefOid --jq .baseRefOid)"
```

The manual run tests that branch's current commit. It does not replace the
automatic PR check; see [GitHub's required-check event rules](https://docs.github.com/en/pull-requests/how-tos/merge-and-close-pull-requests/troubleshooting-required-status-checks#checks-from-some-workflow-jobs-are-not-evaluated).
Automatic PR validation tests GitHub's pull-request merge revision, including
fork PRs, and compares measured reports against the PR's exact base commit.
Clang Format and Clang Tidy run once inside the same bundle. The Actions branch
selector and command above require a branch in this repository.

The `Full CI` job succeeds only when every suite succeeds. A failed, skipped,
or cancelled suite fails that job. Set `Full CI` as a required status check in
the target branch's ruleset to prevent merging before the complete run passes.
Manual smoke runs do not produce a passing `Full CI` result.

## Optional manual smoke profile

Use `-f profile=pr` only for an additional manual smoke run. It does not replace
the automatic full PR run.

The matrix selector runs before creating platform jobs. Unknown profiles,
invalid matrix data, and selector failures fail the planning job. There is no
fallback that silently starts the exhaustive matrix.

| Environment | Configuration |
| --- | --- |
| Ubuntu 25.10, Clang 20 | Debug |
| Ubuntu 24.04, GCC | Release |
| Fedora 43, LLVM 22 | ASan + UBSan |
| Windows, LLVM 22 | RelWithDebInfo, plus the MSVC RelWithDebInfo step |
| macOS, Homebrew LLVM 23 | Debug |

All five build the authored targets and run `ci-smoke` tests. This label covers
core unit tests, default authored suites, public API/script contracts,
discovery, one module-registration/mock integration, and small codegen-driver
regressions. Empty selections fail. Package consumers are disabled in these
five jobs. Lint and recording with both real serializers remain in the routine
bundle.

The automatic full bundle also exercises exhaustive CMake/package/mock/module/
shared-library acceptance, coverage, QEMU, alternate build systems, and measured
comparisons. See [private local validation](local_validation.md) for the local
run/report command. Local evidence is never sent to GitHub by these scripts.

## Full GitHub validation

```bash
gh workflow run ci.yml --ref <candidate-branch> -f profile=full -f base_ref=<base-commit>
```

`full` selects the retained compatibility matrix (31 Linux, five Windows, eight
macOS jobs), complete configured CTest inventories, the package workflow, lint,
coverage, aarch64/riscv64 QEMU, Bazel/Meson/Xmake, measured comparisons, recording,
and the isolated Clang leak diagnostics. Each suite also has its own manual
entry point. All reusable workflows check out the caller's commit. Windows Clang
and MSVC validation use `RelWithDebInfo`, including explicit build and CTest
configuration selection.

The two Clang 20 ASan jobs that failed inside libclang are replaced by isolated,
strict XFAIL probes. Ordinary Clang 20 compatibility and other sanitizer lanes
remain. **The probes do not provide end-to-end Clang 20 sanitizer coverage.**
That coverage gap remains until the relevant upstream fixes are available in
supported packages and the full lanes can be restored. Neither libclang nor
Gentest leak detection is patched or globally disabled by this change.

See the [diagnostic ownership leak](issues/libclang_dependent_diagnostic_leak.md)
and [deduction-failure ownership leak](issues/libclang_deduction_failure_leak.md).
`gh workflow run clang_leaks.yml --ref <candidate-branch>` runs just the probes.
Only logs generated on GitHub runners are uploaded by that workflow.

## Known Windows Debug limit

With `GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS=ON`, the discovery and recording
fixtures skip only their direct abort sub-probes in the actual Debug
configuration and print a known-skip notice. The discovery notice includes:

> Application may suspend for debugger attachment.

Ordinary discovery, recording scope/lifecycle/export checks, non-aborting death
checks, and process-launch failure checks still run. Release keeps the abort
probes. The existing runtime death-test skips remain unchanged. We do not
change debugger settings or work around the Debug CRT behavior.

## Candidate and merge discipline

Rebase or merge the target into the candidate before validation. Manual CI
tests the selected branch commit, not a synthetic merge. A rebase or code change
requires fresh evidence for the resulting commit. Validate the final integration
candidate once, then merge the stack from bottom to top without rewriting its
validated content. Keep local evidence private; any public check description
should contain only a deliberately reviewed, minimal result summary.

Inspect the routine inventory with:

```bash
ctest --test-dir build/debug-system --show-only=json-v1 -L '^ci-smoke$'
```

Full local runs omit label filters. Nested helper builds are capped at one job;
Xmake helpers retain their CTest resource lock. No persistent producer, textual
parse, or PCM cache is introduced.
