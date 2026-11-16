# Clang constraint-deduction failure ownership leak

The second observed Clang 20 LeakSanitizer failure comes from
`clang::MakeDeductionFailureInfo`, separate from the
[deferred diagnostic leak](libclang_dependent_diagnostic_leak.md).
It matches upstream [LLVM issue #143129](https://github.com/llvm/llvm-project/issues/143129).
This page and its standalone probe do not patch LLVM or disable sanitizers.

## Ownership path and reproduction

In LLVM 20.1.8, the `ConstraintsNotSatisfied` branch allocates saved failure
information in the AST arena and copies `AssociatedConstraintsSatisfaction`.
The copied satisfaction contains a `SmallVector<TemplateArgument, 4>`. More than
four arguments require separately allocated storage. The corresponding
`DeductionFailureInfo::Destroy()` path clears the saved pointer without destroying
that copied satisfaction object. Reclaiming the arena does not reclaim its
separately allocated vector storage.

[`tests/clang_leaks/probe.cpp`](../../tests/clang_leaks/probe.cpp) creates eight
template arguments, calls that exact path, explicitly invokes `failure.Destroy()`,
and destroys the original deduction information and AST. Its control performs
the same setup and teardown without creating the arena copy. The completion
marker is flushed after destruction, before LSan can terminate the process
without flushing standard output.

On normally packaged apt.llvm.org Clang 20.1.8, the control exits cleanly and the
candidate reports one 216-byte allocation with a symbolized
`clang::MakeDeductionFailureInfo` frame. The exact byte count is allocator and
version dependent and is not the expected-failure criterion. With normally
packaged Clang 22.1.8, the same candidate is clean: the wrapper deliberately
reports XPASS and returns nonzero so maintainers review the expectation.

## Probe and acceptance policy

Configure this standalone project with a normally installed compatible LLVM:

```bash
cmake -S tests/clang_leaks -B build/clang-leaks -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/clang-leaks
python3 scripts/probe_clang_leaks.py --binary build/clang-leaks/clang_leak_probe --probe deduction --output <new-private-probe-directory>
```

`--output` must name a directory that doesn't exist yet. The wrapper writes
`control.log`, `candidate.log`, `result.json`, and `summary.md` there. It runs
the probe with `ASAN_OPTIONS=detect_leaks=1:fast_unwind_on_malloc=0:symbolize=1`
and `LSAN_OPTIONS=exitcode=86`, replacing any values in your environment, and
exits with 0 only for the expected failure (XFAIL).

The strict wrapper requires a successful control, a completed candidate
ownership path, LSan's dedicated exit code, symbolized known frames in **every**
allocation, and complete matching allocation totals. A clean candidate is XPASS
and fails. Unknown or mixed leaks, missing symbols, setup errors, signals, and
timeouts fail. No broad `continue-on-error` or global suppression is used. The
`dependent` probe uses the same policy for `DependentDiagnostic::Create`.

## Patch direction

For an upstream change or supported package update, ensure destruction of the
saved constraint-satisfaction object's owning members before the arena-backed
record is discarded. Keep ownership of the separately stored template-argument
list and optional diagnostic consistent with the other deduction-result paths;
do not add a second deletion of arena storage. Add an upstream regression with
more arguments than the inline capacity and run both the owning control and
failure path under LSan. Prefer consuming the upstream fix through supported
LLVM packages over maintaining a gentest-specific LLVM fork.

## In CI

The `clang_leaks.yml` workflow runs both Clang 20 probes as separate jobs: the
`dependent` probe against Ubuntu 25.10's Clang 20 package, and the `deduction`
probe against apt.llvm.org's Clang 20 on Ubuntu 24.04. Full CI includes it,
and you can also start it by hand. It uploads only the diagnostics it produced.
CI has no Clang 20 AddressSanitizer build until fixed packages can restore one;
the isolated probes don't replace that end-to-end coverage.
