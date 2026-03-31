# Story: Official Downstream Support For Bazel, Xmake/xrepo, and Meson

## Goal

Define what must be true before gentest can honestly claim official downstream
support outside CMake.

Today:

- CMake is the primary packaged downstream path.
- Bazel is repo-local and promising.
- Xmake is repo-local and promising.
- Meson is repo-local and textual-only.

This story turns that qualitative status into explicit promotion criteria.

## Problem

The project already has real non-CMake integrations, but they are not yet
shippable as stable downstream products.

The current gap is not only "tests missing". It is a product-shape gap:

- the helper APIs are still repo-local
- packaging/export/install flows are incomplete
- host-tool configuration is still toolchain-sensitive
- downstream users would need repo knowledge that should be encoded in the
  integration itself

That means the answer today is still:

- good enough for checked-in repo smoke coverage
- not yet strong enough for "consume this as a normal external dependency"

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

We can call Bazel officially supported only when all of this is true:

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

We can call Xmake officially supported only when all of this is true:

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

We can call Meson officially supported only when all of this is true:

1. There is a reusable Meson helper/API instead of a repo-local handwritten
   `meson.build` path.
2. The textual path is packaged and documented as a real downstream flow.
3. If module support is claimed, it must exist as an intentionally supported
   feature with real CI coverage.
4. The `codegen_path` / `codegen_host_clang` contract is explicit and stable.
5. There is at least one downstream-style example that consumes gentest without
   copying repo-private Meson logic.

Until then, Meson should remain explicitly documented as repo-local and
textual-only.

## Common readiness criteria

Every non-CMake backend should meet these shared requirements before "official
support" is claimed:

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

Promote Bazel first because the current rule surface is already closest to a
real downstream integration.

Deliverables:

- stable Starlark surface
- Bzlmod/external-repo packaging
- downstream example repo or in-tree installed example
- CI that validates textual and module consumers as downstream users would run
  them

### Phase 2: Xmake / xrepo

Promote Xmake next, after packaging the helper layer and `gentest_codegen`.

Deliverables:

- xrepo/package layout
- stable Lua helper surface
- downstream example using package consumption instead of repo-local includes
- cross-host CI proof for textual and modules

### Phase 3: Meson

Promote Meson last, and textual-only first.

Deliverables:

- reusable Meson helper/API
- packaged textual flow
- explicit statement on whether modules are unsupported or promoted later

## Acceptance criteria

- There is a documented checklist for when each non-CMake backend may be called
  officially supported.
- The checklist distinguishes repo-local validation from real downstream
  packaging.
- Bazel, Xmake/xrepo, and Meson each have a concrete promotion path instead of
  vague "future support" wording.
- Docs and CI work can now be tracked against explicit readiness gates rather
  than broad intent.
