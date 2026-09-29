# Libclang deferred diagnostic storage leak

Investigated on 2026-09-27. **Status: upstream patch proposal only.** The fix
below has not been applied to LLVM, built, or validated against a patched
library. Gentest's codegen and sanitizer settings remain unchanged.

## Observed failure

At Gentest commit `8e6dd7fa9350f9ef5a1208bccf8da1452ab7d999`, the
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
reproducer. The API probe below isolates the diagnostic ownership defect
without Gentest.

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
what the control case below exercises.

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

## Standalone ownership probe

Save this as `diagnostic_lifetime_probe.cpp`. It deliberately constructs three
heap-backed diagnostic strings of lengths 30, 31, and 31. The observed 95-byte
total includes their terminators on the tested implementation; that exact
allocation size is not a portable assertion.

```cpp
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/DependentDiagnostic.h>
#include <clang/Basic/PartialDiagnostic.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Tooling/Tooling.h>
#include <string>

int main(int argc, char **) {
    auto ast = clang::tooling::buildASTFromCodeWithArgs(
        "template<class T> struct DiagnosticProbe {};", {"-std=c++20"});
    if (!ast)
        return 2;
    auto &context = ast->getASTContext();
    for (auto *decl : context.getTranslationUnitDecl()->decls()) {
        auto *template_decl = llvm::dyn_cast<clang::ClassTemplateDecl>(decl);
        if (!template_decl)
            continue;
        auto *record = template_decl->getTemplatedDecl();
        clang::PartialDiagnostic diagnostic(0, context.getDiagAllocator());
        diagnostic << std::string(30, 'a') << std::string(31, 'b') << std::string(31, 'c');
        if (argc == 1)
            clang::DependentDiagnostic::Create(context, record, clang::DependentDiagnostic::Access,
                                               {}, true, clang::AS_private, record, record, {}, diagnostic);
        return 0;
    }
    return 3;
}
```

The control constructs the same AST and original diagnostic but omits the
arena copy. Exit codes 2 and 3 indicate that the probe failed to reach the
intended path and must not be interpreted as successful leak detection.

Commands verified with normally installed Fedora LLVM/Clang 22.1.8 development
packages and ASan. Library names and search paths can differ on other systems.

```bash
clang++ -std=c++20 -g -fsanitize=address -fno-omit-frame-pointer \
  diagnostic_lifetime_probe.cpp -lclang-cpp -lLLVM-22 \
  -o diagnostic_lifetime_probe

# Original diagnostic only: expected exit 0, without a leak report.
ASAN_OPTIONS=detect_leaks=1 ./diagnostic_lifetime_probe control

# Arena copy: expected LeakSanitizer report with the unpatched library.
ASAN_OPTIONS=detect_leaks=1 ./diagnostic_lifetime_probe
```

Observed locally:

| Case | Exit | Result |
| --- | --- | --- |
| Control, no arena copy | 0 | No LeakSanitizer report |
| Deferred diagnostic copy | 1 | Three leaked allocations totaling 95 bytes |

The second case leaks with LLVM 22.1.8 as well. A successful full Gentest CI
lane on another compiler/standard-library combination therefore does not
establish that this ownership path has been fixed. This synthetic probe does
not establish that every toolchain reaches it through the same source
expression.

## Validation required before adopting the patch

- Add an upstream regression that destroys the AST context while
  LeakSanitizer is active. Cover empty diagnostics, heap-backed strings,
  repeated diagnostics, and independently destroyed AST contexts.
- Build a patched libclang and run both probe paths. Both should exit cleanly,
  without suppressions or disabled leak detection.
- Run the original Gentest mocking codegen invocation with the patched LLVM
  20 library and the original sanitizer configuration.
- Run relevant Clang AST/Sema and serialization tests to check deferred
  diagnostic lifetime, cleanup ordering, and unchanged diagnostic behavior.

The patch belongs in LLVM. Gentest CI would need an installed LLVM/libclang
package containing it, through an upstream release or a maintained backport.
Use supported package/build tooling and retain provenance; do not vendor
compiler binaries or introduce a private diagnostic-cleanup workaround into
Gentest.

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
