# Refactor Plan Checklist

Guidelines
- Each item is a separate commit. Do not check off until tests are run and the commit is created.
- If one change satisfies multiple overlapping items, note the shared commit and check all covered items.
- Default tests per item: `cmake --build --preset=debug-system --target gentest_unit_tests` then `ctest --preset=debug-system --output-on-failure`.

Simplify
- [x] Replace `MockParamInfo` 3-boolean ref flags with a compact enum / pass-style to reduce branching.  
  Tests: default. Commit: isolated to model/discovery/render.
- [ ] Extract shared param-info builder for ctor/method parameter extraction to remove duplicated type/name/ref handling.  
  Tests: default. Commit: discovery-only change.
- [ ] Normalize `ensure_global_qualifiers` usage to a single pass (avoid double-qualification in callers).  
  Tests: default. Commit: render_mocks only.
- [ ] Refactor `pointer_type_for` to reuse `qualifiers_for` instead of reformatting cv/ref/noexcept.  
  Tests: default. Commit: render_mocks helpers only.
- [ ] Factor out shared dispatch emission between `build_method_declaration` and `method_definition`.  
  Tests: default. Commit: render_mocks only.

Performance
- [ ] Stream parameter combinations in discovery (avoid full cartesian materialization + std::map copies).  
  Tests: default. Commit: discovery-only; note perf rationale.
- [ ] Cache per-record fixture/attribute validation outside `add_case` to avoid repeated AST scans.  
  Tests: default. Commit: discovery-only; keep behavior identical.
- [ ] Precompute normalized type strings for parameter ranges to avoid per-value string rebuilds.  
  Tests: default. Commit: discovery-only.
- [ ] Remove double `ensure_global_qualifiers` passes in render paths (join_* vs outer callers).  
  Tests: default. Commit: may overlap with Simplify/qualifier pass; cross-reference.
- [ ] Simplify `close_namespaces` to compute component count once.  
  Tests: default. Commit: render_mocks only.
- [ ] Avoid copying mocks vector just to sort (sort indices or move if allowed).  
  Tests: default. Commit: render_mocks only.
- [ ] Reduce logging allocations by writing fmt buffer directly to `llvm::errs()` (skip to_string).  
  Tests: default. Commit: tools/src/log.hpp only.

Extensibility
- [ ] Centralize forwarding policy (single helper owns move/forward/copy decision) to avoid split logic between discovery/render.  
  Tests: default. Commit: render+discovery; keep helper in one place.
- [ ] Unify constructor forwarding with method argument forwarding (avoid separate hard-coded path).  
  Tests: default. Commit: render_mocks only unless policy helper changes.
- [ ] Expand `MockParamInfo` to carry cv/value-category metadata for future policies.  
  Tests: default. Commit: model+discovery+render update.
- [ ] Broaden forwarding-ref detection beyond bare `TemplateTypeParmType`/`auto` (handle aliases/substituted template types).  
  Tests: default. Commit: discovery-only; add focused tests if behavior changes.
- [ ] Add explicit policy hook for by-value parameters (copy vs move) in argument emission.  
  Tests: default. Commit: render_mocks only; document default policy.
