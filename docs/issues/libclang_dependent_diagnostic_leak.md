# Libclang deferred diagnostic storage leak

Investigated on 2026-09-27. **Status:** the fix below is a proposal for LLVM.
It has not been applied to LLVM, built, or validated against a patched
library. gentest keeps leak detection on and adds no suppressions. Full CI runs
the [repository probe](#reproduce-it) against Ubuntu 25.10's Clang 20 package
and expects this leak. CI has no Clang 20 AddressSanitizer build.

## Observed failure

At gentest commit `8e6dd7fa9350f9ef5a1208bccf8da1452ab7d999`, the
[Ubuntu 25.10 / Clang 20 ASan+UBSan job](https://github.com/jkammerland/gentest/actions/runs/36269281859/job/108496237209)
failed while `gentest_codegen` scanned `tests/mocking/cases.cpp`. The library
was Ubuntu's `libclang-cpp20` package `1:20.1.8-0ubuntu4`, with libstdc++ 15
headers. LeakSanitizer reported three leaked string buffers: 31, 32, and 32
bytes. Its nonzero exit stopped the build before the tests ran.

The relevant allocation stack, in caller-to-callee order, is:

```text
Sema::DeduceTemplateSpecializationFromInitializer
TemplateDeclInstantiator::VisitCXXDeductionGuideDecl
Sema::instantiateExplicitSpecifier
Sema::CheckUnresolvedLookupAccess
DependentDiagnostic::Create
std::basic_string::_M_assign
operator new

SUMMARY: AddressSanitizer: 95 byte(s) leaked in 3 allocation(s).
```

This establishes a trigger involving class-template argument deduction,
conditional explicit-specifier substitution, and deferred access checking.
The exact original C++ expression has not been reduced to a minimal source
reproducer. The [repository probe](#reproduce-it) isolates the diagnostic
ownership defect without gentest.

## Ownership defect

Clang defers some access diagnostics until a dependent context is resolved.
In LLVM 20.1.8:

1. [`DependentDiagnostic::Create`](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.8/clang/lib/AST/DeclBase.cpp#L2216)
   allocates `DiagnosticStorage` and the diagnostic wrapper in the
   `ASTContext` bump allocator.
2. The [`PartialDiagnostic` copy constructor](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.8/clang/include/clang/Basic/PartialDiagnostic.h#L81)
   copies the original storage into that arena-owned storage.
3. [`DiagnosticStorage`](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.8/clang/include/clang/Basic/Diagnostic.h#L153)
   contains owning `std::string` arguments and small-vector members. Their
   backing allocations can live outside the arena.
4. The dependent diagnostic list does not register destruction of this
   storage. Releasing the arena reclaims the object's bytes but does not run
   its destructor, leaving those separate allocations behind.

The allocator for the original partial diagnostic cleans up its own storage.
It does not own the copied storage in the AST arena. That distinction is
what the probe's control case exercises.

## Proposed upstream patch

Target: `clang/lib/AST/DeclBase.cpp` in LLVM, with context from tag
`llvmorg-20.1.8`.

```diff
--- a/clang/lib/AST/DeclBase.cpp
+++ b/clang/lib/AST/DeclBase.cpp
@@ -2229,5 +2229,7 @@
   DiagnosticStorage *DiagStorage = nullptr;
-  if (PDiag.hasStorage())
+  if (PDiag.hasStorage()) {
     DiagStorage = new (C) DiagnosticStorage;
+    C.addDestruction(DiagStorage);
+  }

   auto *DD = new (C) DependentDiagnostic(PDiag, DiagStorage);
```

[`ASTContext::addDestruction`](https://clang.llvm.org/doxygen/classclang_1_1ASTContext.html)
registers a destructor callback for a nontrivially destructible object. The
proposal registers the storage exactly once, when it is allocated, so its
owning members are destroyed during context cleanup. The arena continues to
own the object's memory; calling `delete DiagStorage` would be incorrect.
Register the `DiagnosticStorage`, rather than the enclosing partial-diagnostic
wrapper, whose allocator bookkeeping describes arena-backed storage.

This is a candidate fix, not a demonstrated before/after result. Check cleanup
ordering and all other owners before upstream submission.

## Reproduce it

[`tests/clang_leaks/probe.cpp`](../../tests/clang_leaks/probe.cpp) reproduces
the ownership path in its `dependent` mode. It builds a small AST, streams
three heap-backed strings of lengths 30, 31, and 31 into a partial diagnostic,
and copies that diagnostic into the AST arena with
`DependentDiagnostic::Create`. The control, `dependent control`, builds the
same AST and diagnostic but skips the arena copy. The probe prints
`PROBE_COMPLETED` only after the diagnostic and the AST are destroyed. Exit
codes 2 to 5 mean it never reached the intended path, which is not a leak
result.

Build the probe against a normally installed LLVM and run it through the strict
wrapper:

```bash
cmake -S tests/clang_leaks -B build/clang-leaks -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/clang-leaks
python3 scripts/probe_clang_leaks.py --binary build/clang-leaks/clang_leak_probe --probe dependent --output <new-private-probe-directory>
```

The wrapper's pass and fail rules are in the deduction-failure page's
[probe and acceptance policy](libclang_deduction_failure_leak.md#probe-and-acceptance-policy).

Observed on 2026-09-27 with Fedora's LLVM 22.1.8 packages (see
[Recorded provenance](#recorded-provenance)), using a standalone copy of this
probe with `ASAN_OPTIONS=detect_leaks=1` and no wrapper:

| Case | Exit | Result |
| --- | --- | --- |
| Control, no arena copy | 0 | No LeakSanitizer report |
| Deferred diagnostic copy | 1 | Three leaked allocations totaling 95 bytes |

The 95-byte total includes the strings' terminators on the tested
implementation; it is not a portable expectation. The wrapper sets
`LSAN_OPTIONS=exitcode=86`, so under the wrapper a leak exits with 86.

So the arena copy leaks with LLVM 22.1.8 as well as 20.1.8. A successful full
gentest CI job on another compiler or standard library therefore does not show
that this ownership path is fixed. The probe also does not show that every
toolchain reaches the path through the same source expression.

## Validation required before adopting the patch

- Add an upstream regression that destroys the AST context while
  LeakSanitizer is active. Cover empty diagnostics, heap-backed strings,
  repeated diagnostics, and independently destroyed AST contexts.
- Build a patched libclang and run both probe paths. Both should exit cleanly,
  without suppressions or disabled leak detection. The wrapper then reports
  XPASS and fails on purpose, so that maintainers review the expectation.
- Run the original gentest mocking codegen invocation with the patched LLVM
  20 library and the original sanitizer configuration.
- Run relevant Clang AST/Sema and serialization tests to check deferred
  diagnostic lifetime, cleanup ordering, and unchanged diagnostic behavior.

The patch belongs in LLVM. gentest CI would need an installed LLVM/libclang
package containing it, through an upstream release or a maintained backport.
Use supported package and build tooling and retain provenance; do not vendor
compiler binaries or add a private diagnostic-cleanup workaround to gentest.

## Recorded provenance

Local probe on 2026-09-27: x86_64 Fedora, Clang 22.1.8
(`22.1.8-4.fc44`), libclang-cpp 22.1, and libstdc++ 16 headers. These hashes
identify the observed tools; they are not installation requirements.

```text
/usr/bin/clang-22
SHA-256: 4ea85504401d9f5c05109d6cb05778b241aa8d56fde1da2e5bc7ba46cffd44f5

/usr/lib64/libclang-cpp.so.22.1
SHA-256: 96b7ed1028bf3750a8e3b4e68ffe1054869dd505ed1fd06b9dce08cac21832d9
```
