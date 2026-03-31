# Story: Windows LLVM 20 MatchFinder Traversal Crash In Module Codegen

## Goal

Identify and fix the real Windows LLVM 20 codegen failure without regressing
cross-platform discovery semantics.

This story exists because the first workaround fixed the Windows crash by
replacing Clang's normal matcher traversal with a manual declaration walk, and
that workaround regressed template-driven discovery on Linux/Fedora.

## Real Failure

The real failure is not "template attributes are broken." That was the
regression caused by the workaround.

The real failure is:

- `gentest_codegen` can crash with `0xC0000005` on Windows when using LLVM
  20.x
- the crash happens in the normal codegen discovery path, not in explicit mock
  discovery
- the failing surface is the named-module / external-module path used by tests
  such as:
  - `gentest_module_mock_imported_sibling`
  - `gentest_module_mock_multi_imported_sibling`
  - `gentest_explicit_mock_target_install_export`
  - `gentest_module_header_unit_import_preamble`

The current best theory is that the crash is triggered by the combination of:

- `MatchFinder` recursive traversal
- `traverse(TK_IgnoreUnlessSpelledInSource, ...)`
- `ASTContext::setTraversalScope(...)`
- imported/generated named modules
- Windows LLVM 20.x

That boundary is well supported by observed behavior, but the exact LLVM
internal crash site is still unproven until we capture a native debugger stack.

## Rejected Workaround

The workaround in commit `d6c4bd7` replaced normal matcher traversal with a
manual declaration walk rooted at `TranslationUnitDecl::decls()`.

That is incorrect because it only recursed through `DeclContext` children and
therefore skipped template wrappers such as `FunctionTemplateDecl`.

Observed regression symptoms:

- `gentest_codegen_check_invalid_duplicate_template_attr` stops validating
  template attributes
- `templates_inventory` drops from `93` cases to `49`
- `benches_filter_space` stops seeing template-generated benchmarks

This workaround must be removed rather than patched further.

## User Stories

### 1. Windows LLVM 20 support decision

As a maintainer, I want to know whether this is a real gentest bug or an LLVM
20 Windows toolchain bug, so we can either fix gentest properly or set an
honest support floor.

### 2. Cross-platform discovery correctness

As a maintainer, I want any Windows fix to preserve existing Linux/macOS
discovery behavior for:

- template test matrices
- parameterized cases
- benchmark/jitter discovery
- fixture discovery

### 3. Named-module stability

As a maintainer, I want imported/generated module topologies to work on Windows
without access violations, so explicit module-mock exports remain usable.

### 4. Clear downgrade policy if needed

As a user, I want a clear statement if Windows module/mock codegen requires
LLVM 21+ rather than LLVM 20, so I do not waste time debugging an unsupported
toolchain.

## Investigation Plan

1. Revert the manual traversal workaround from `d6c4bd7`.
2. Keep unrelated hardening only if it is independently justified.
3. Produce a native Windows LLVM 20 repro under a debugger and capture the
   stack.
4. Test whether the crash still occurs when we avoid
   `ASTContext::setTraversalScope(...)` but keep Clang's normal matcher
   traversal.
5. If the crash survives that change, move normal discovery to a real
   `RecursiveASTVisitor`-based collector instead of ad-hoc traversal.
6. If the crash is confirmed to be an LLVM 20 Windows bug with no safe gentest
   workaround, document and enforce a Windows LLVM 21+ requirement for the
   module/mock codegen path.

## Preferred Fix Direction

The first design to try is:

- keep `finder.newASTConsumer()`
- stop using `ASTContext::setTraversalScope(...)` for normal discovery
- filter top-level declarations before forwarding them to the matcher consumer

This keeps Clang's own traversal semantics intact while avoiding the currently
suspect scope-mutation path.

If that still crashes, the next design is:

- use `RecursiveASTVisitor` for normal test/fixture discovery
- keep explicit mock discovery separate if needed

What we should not do is keep extending a hand-written recursive declaration
walk with special cases for `FunctionTemplateDecl`, `ClassTemplateDecl`, and
other AST wrapper nodes.

## Acceptance Criteria

- The Windows LLVM 20 failing slice no longer crashes.
- `gentest_codegen_check_invalid_duplicate_template_attr` passes again.
- `templates_inventory` returns to `93` cases.
- `benches_filter_space` passes again.
- The fix is proven on Linux, Windows, and at least one native non-Windows host.
- If LLVM 20 Windows remains unsupported, the code and docs fail clearly and
  intentionally rather than crashing.
