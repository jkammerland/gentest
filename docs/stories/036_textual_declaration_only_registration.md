# Story: rejected declaration-only textual registration

## Status

Rejected on 2026-04-21.

## Decision

Do not add declaration-only textual registration.

Textual `.cpp` tests keep wrapper/include mode as the compatibility path. That
mode is explicit in the artifact manifest through `includes_owner_source: true`
and `replaces_owner_source: true`, and it remains the only path that preserves
anonymous namespaces, `static` tests, source-local fixtures, source-local helper
types, benchmarks, jitters, and mocks without forcing users to move declarations
into headers.

Named modules remain the strategic path for declaration-free registration. For
module test sources, same-module registration avoids including the authored
`.cppm` source while still allowing non-exported tests and fixtures in the
module purview.

## Rationale

The rejected proposal would have added an opt-in textual mode that generated
standalone registration sources for `.cpp` tests without including the owning
source file. That only works when every called test function and every required
fixture/signature type is visible from another translation unit.

That tradeoff is not worth the additional protocol surface:

- it adds CLI/build-system switches for a narrow transitional mode
- it adds manifest semantics that are not needed by the default textual path
- it needs codegen-owned eligibility diagnostics for anonymous namespaces,
  `static` functions, source-local fixtures, and source-local helper types
- it creates more non-CMake parity work before the wrappers have been collapsed
  to thin manifest consumers
- it encourages a source style that modules replace more cleanly

The long-term cleanup direction is therefore:

- keep textual wrapper/include mode for `.cpp` compatibility
- keep same-module registration for `.cppm` sources
- make build-system wrappers consume tool-owned manifests instead of
  reimplementing C++ semantics
- do not add a second textual registration protocol

## Consequences

- Story `037` must not gate cleanup on declaration-only textual registration.
- Cleanup that previously depended on this story should instead preserve
  manifest-declared textual wrapper semantics.
- Source-inspector and wrapper cleanup should proceed through story `033`,
  story `015`, and the release-window gates tracked by story `037`.
- Documentation should describe declaration-only textual registration as a
  rejected alternative, not future work.

## Rejected Proposal

The proposal was:

- add an explicit opt-in CLI/build-system switch for declaration-only textual
  registration
- define manifest entries for declaration-only textual outputs, including
  `includes_owner_source: false` and `replaces_owner_source: false`
- generate standalone registration sources for eligible textual tests
- reject or clearly diagnose source-local tests, anonymous-namespace tests,
  `static` tests, and fixture/signature types that are not visible to the
  generated translation unit

This remains rejected unless a future design shows a concrete user need that is
not better served by modules or by the existing textual wrapper mode.
