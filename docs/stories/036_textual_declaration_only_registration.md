# Story: additive textual header-declaration registration

## Status

Superseded by the issue #107 next-major decision on 2026-08-13.

## Decision

The 2026-04-21 rejection applied to a secondary opt-in declaration-only mode.
Issue #107 deliberately changes the single ordinary textual CMake model instead:

- Gentest attributes are written on header declarations.
- Authored `.cpp` files stay attached to the target and compile normally.
- Gentest appends one stable generated registration source per internal scan
  slot; generated sources never include an authored `.cpp`.
- A declaration may be defined out of line in a linked translation unit, or be
  an external-linkage inline definition in its header.
- Source-only and internal-linkage annotations are rejected with migration
  diagnostics.

The public call remains `gentest_attach_codegen(target)`. Source IDs, header
suffixes, ownership maps, generated includes, and include-root declarations are
not part of the user protocol.

## Rationale

Wrapper substitution removed the authored source from the target graph and its
own `compile_commands.json` entry. The additive model restores conventional
header/source separation, exact authored compile commands, clangd behavior, and
unity/source ownership while retaining semantic Clang discovery.

The implementation accepts a deliberate migration cost: declarations and all
types needed by generated adapters must be header-reachable and self-contained
under the selected compile context. Anonymous-namespace and `static` tests do
not have a stable target-wide entity identity and remain unsupported in this
model.

## Processing contract

Textual discovery uses isolated indexed scan results, a deterministic serial
merge, and parallel emission. The merge distinguishes physical declaration
sites from stable C++ entities, validates semantic fingerprints across compile
contexts and redeclarations, merges fixtures before case dependency
resolution, and chooses target-local ownership by stable scan-slot order.

Explicitly listed headers not reached by an authored translation unit receive
predeclared fallback slots. Reached fallback slots and otherwise unassigned
slots still emit valid empty sources so output paths never depend on discovery
or worker order.

## Compatibility

The previous-major ordinary wrapper/include backend was removed after repository
and downstream fixtures migrated. Named-module-authored tests use
`MODULE_REGISTRATION FILE_SET`; explicit named-module mock generation remains a
separate internal pipeline rather than a test-registration fallback.
