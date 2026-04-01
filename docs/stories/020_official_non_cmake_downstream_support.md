# Story: Official Downstream Support For Bazel, Xmake/xrepo, and Meson

## Goal

Define and then implement the downstream support contract outside CMake.

Status in this branch:

- CMake remains the primary packaged downstream path.
- Bazel now has an official checked-in Bzlmod/source-package surface.
- Xmake now has an official checked-in xrepo/install-helper surface.
- Meson now has an official checked-in textual wrap/subproject surface.

The remaining work is publication polish and broader CI/runtime proof, not
inventing the downstream contract from scratch.

## Problem

The project had real non-CMake integrations, but they were not yet shippable as
stable downstream products.

The original gap was not only "tests missing". It was a product-shape gap:

- helper APIs were still repo-local
- packaging/export/install flows were incomplete
- host-tool configuration was still toolchain-sensitive
- downstream users needed repo knowledge that should have been encoded in the
  integration itself

This story closes that gap with real downstream fixtures and public entrypoints.

## User stories

### 1. Bazel / Bzlmod consumer

As a Bazel user, I want to add gentest through a versioned rule/module package,
so I can use gentest without vendoring the whole repository or copying
repo-local macros into my own tree.

### 2. Xmake / xrepo consumer

As an Xmake user, I want to add gentest as an xrepo/Xmake package that exposes
runtime libraries, helper Lua glue, and `gentest_codegen`, so I can use gentest
without depending on repo-local paths or private helper files.

### 3. Meson consumer

As a Meson user, I want a supported gentest package/wrap/subproject story with
an explicit helper API, so I can use at least the textual path without writing
my own custom codegen plumbing.

### 4. Honest support claims

As a maintainer, I want the documentation and support matrix to reflect the real
backend maturity level, so "officially supported" means a downstream user can
reasonably expect stable packaging, configuration, and CI coverage.

## Backend-specific promotion bar

### Bazel

Implemented shape:

1. The current helpers in `build_defs/gentest.bzl` are promoted into a stable,
   versioned downstream surface.
2. There is a real Bzlmod / external-repo story, not just repo-local `BUILD`
   examples.
3. The host-tool contract is explicit and documented:
   - `gentest_codegen`
   - host Clang
   - optional `clang-scan-deps`
4. The public API is stable enough that downstream examples do not need to know
   generated wrapper/header naming details.
5. CI proves textual and module paths on the supported host/compiler matrix.
6. The checked-in smoke checks are not relying on workspace-only assumptions
   such as hardcoded `bazel-bin` paths.

### Xmake / xrepo

Implemented shape:

1. The helper layer in `xmake/gentest.lua` is packaged and versioned for
   downstream use.
2. There is a clear xrepo/Xmake package shape that exposes:
   - runtime libraries
   - helper Lua glue
   - host `gentest_codegen`
3. The Windows Clang path is reliable enough that downstream users do not need
   repo-specific workarounds.
4. Module and textual paths both have validated downstream-style examples.
5. CI proves Linux, macOS, and Windows textual/module consumer flows.
6. Path-length-sensitive harness behavior is reduced enough that the package
   contract is not hostage to one repo's test layout.

### Meson

Implemented shape:

1. There is a reusable Meson downstream contract instead of copying
   repo-private Meson files by hand.
2. The textual path is packaged and documented as a real downstream flow.
3. If module support is claimed, it must exist as an intentionally supported
   feature with real CI coverage.
4. The `codegen_path` / `codegen_host_clang` contract is explicit and stable.
5. There is at least one downstream-style example that consumes gentest without
   copying repo-private Meson logic.

Meson remains explicitly textual-only.

## Common readiness criteria

Every non-CMake backend should meet these shared requirements for "official
support":

1. Downstream package shape exists.
2. Host-tool contract is explicit.
3. Generated-output naming details are hidden behind the backend helper API.
4. Textual mock path works end to end.
5. Module mock path works end to end if module support is claimed.
6. Consumer smoke coverage includes:
   - plain test
   - mock-backed test
   - bench
   - jitter
7. CI covers at least Linux and one native secondary host for each claimed
   backend, with Windows included for any backend that claims Windows support.
8. Docs no longer describe the backend as "repo-local only".

## Proposed rollout

### Phase 1: Bazel

Delivered:

Deliverables:

- stable Starlark surface
- Bzlmod/external-repo packaging
- downstream example repo or in-tree installed example
- CI that validates textual and module consumers as downstream users would run
  them

### Phase 2: Xmake / xrepo

Delivered:

Deliverables:

- xrepo/package layout
- stable Lua helper surface
- downstream example using package consumption instead of repo-local includes
- cross-host CI proof for textual and modules

### Phase 3: Meson

Delivered:

Deliverables:

- reusable Meson helper/API
- packaged textual flow
- explicit statement on whether modules are unsupported or promoted later

## Acceptance criteria

- Bazel, Xmake/xrepo, and Meson each have a checked-in downstream fixture and a
  documented support contract.
- The docs distinguish textual-only Meson support from full textual+module
  support in Bazel and Xmake.
- CI now has dedicated downstream checks instead of only repo-local smoke
  coverage.
