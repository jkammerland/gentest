# CI and on-demand validation

Pull requests and pushes to `master` automatically run Clang Format and Clang
Tidy with the LLVM 23 toolchain selected by `scripts/llvm_toolchain.json`.
PR lint runs on open, synchronization, reopening, and becoming ready for review,
including draft and documentation-only changes. It has no path filter.

Full CI runs **only on demand**, for both PRs and `master`. There is one complete
suite; routine profiles and path-based suite selection have been removed.
Results are informational. These workflows do not alter branch protection or
merge requirements. Public releases continue to publish source archives only.

## Request full CI for a PR

A metadata-only workflow maintains one GitHub Actions bot comment on each PR.
It shows the current head and base commits and offers this task checkbox:

- [ ] Run full CI for this revision

A maintainer can click the checkbox or post a comment whose complete text is:

```text
/full-ci
```

The controller queries the sender's live repository permission. Only `write`,
`maintain`, or `admin` access authorizes a run; author association is not used.
The checkbox additionally requires an unchecked-to-checked edit of the marked
bot comment with its original comment ID and revision metadata. Other comments,
bot edits, and stale events do not authorize execution.

The command uses the live PR snapshot when the request is handled. Both entry
points freeze the current head, base, and synthetic merge commit. The controller
checks the merge commit's parents and rechecks the live PR before starting work.
Conflicts, unavailable mergeability, and revisions that change during validation
are refused; retry the command after GitHub has a conflict-free merge revision.
Fork PRs use the same flow and test the base repository's pinned merge commit.

A duplicate request while that revision is active returns the existing run link.
A newly authorized revision supersedes the previous full bundle. Concurrency
keys include the validated PR number, with distinct prefixes for every suite,
so requests for different PRs cannot cancel one another. Completed or cancelled
runs can be requested again.

Completion is driven by workflow dependencies. The final comment update checks
both live commits and the latest request's run ID; an older run cannot overwrite
a newer request or prompt. Failed, skipped, and cancelled suites all prevent a
successful full-CI aggregate.

The checkbox event path has payload-level tests. Hosted UI acceptance remains a
separate check after these controller definitions land on the default branch:
click the bot comment's checkbox as a maintainer and verify GitHub delivers an
`issue_comment.edited` event and starts the displayed merge SHA. The permanent
`/full-ci` fallback is available if the UI does not deliver that edit event.
No hosted execution is claimed by the local controller tests.

## Trust and permissions

PR metadata workflows execute controller code from the default branch and never
run candidate code. The `issue_comment` coordinator calls local reusable
workflow definitions directly, keeping those definitions at its trusted default
branch revision. Each worker explicitly checks out the frozen candidate SHA
with `persist-credentials: false`. Matrix selection and LLVM provisioning use
the trusted workflow revision before candidate execution.

Only metadata jobs can update the PR comment. Workers have `contents: read`,
receive no inherited secrets, and use restore-only cache actions. GitHub's
low-trust `issue_comment` cache token also restricts writes; called workflows do
not opt into a more privileged cache mode. No GitHub App, dispatch token, or
`actions: write` permission is required. See GitHub's
[reusable-workflow permission and concurrency rules](https://docs.github.com/en/actions/reference/workflows-and-actions/reusing-workflow-configurations)
and [cache action's restore-only usage](https://github.com/actions/cache/blob/main/restore/README.md).

## Manual master validation and full coverage

Run the complete bundle for a master commit with:

```bash
gh workflow run ci.yml --ref master -f base_ref=<comparison-base-commit>
```

The Actions UI has the same entry point. Its run title remains
`CI (full) — master`, which the signed source-release gate requires along with
the exact successful master commit and run ID. Standalone suites retain manual
entry points for diagnosis.

The full CMake matrix contains 33 Linux, three Windows, and eight macOS rows.
Older LLVM/compiler and distribution rows remain compatibility coverage.
Windows rows that produced identical `RelWithDebInfo` builds are consolidated;
all retained LLVM versions still run both Clang and MSVC with explicit build and
CTest configuration selection, including representative package consumers.

Primary LLVM 23 coverage includes full Debug CTest with Allure and Boost.JSON,
ASan/LSan/UBSan, TSan, and a separate libassert configuration without exceptions.
The complete bundle also includes the package workflow, lint, coverage,
aarch64/riscv64 QEMU, Bazel/Meson/Xmake, measured comparisons against the frozen
base, recording with real serializers, and strict Clang 20 leak diagnostics.
No native worker uses the former `ci-smoke` label filter.

The QEMU jobs currently retain their existing smoke presets. Expanding them to
all cross-compatible cases requires a successful full-preset probe on both
architectures; until that probe passes, they do not constitute full cross-suite
coverage. Native workers run the complete configured CTest inventory.

Clang 20 ASan integration remains a documented upstream coverage gap. Its two
isolated strict XFAIL probes reproduce specific libclang ownership leaks; they
do not replace end-to-end Clang 20 sanitizer coverage. Leak detection remains
enabled. See the [diagnostic ownership leak](issues/libclang_dependent_diagnostic_leak.md)
and [deduction-failure ownership leak](issues/libclang_deduction_failure_leak.md).

Local validation remains available through the commands in
[local validation](local_validation.md). Local results are not uploaded by those
scripts. A changed head or base requires fresh evidence for the resulting merge
revision.
