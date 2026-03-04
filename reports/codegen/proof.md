# Codegen Parser/Emitter Robustness Proof (Red Phase)

Date: 2026-03-04  
Branch: `fix/codegen-parser-robustness`

## Scope
Validated findings:
1. `split_arguments` not tracking `< >` splits template commas incorrectly.
2. `parameters_pack` tuple parsing has same `< >` delimiter weakness.
3. Namespace `suite` attribute back-scan misses trailing comments before namespace name.
4. `parse_tu_index` register symbol collision risk for filenames containing same `tu_XXXX` fragment.

All four findings reproduced as failing regressions.

## Red-Phase Commands and Evidence

### 1) `split_arguments` template comma handling (TRUE)
### 2) `parameters_pack` tuple template comma handling (TRUE)
Command:
```bash
ctest --test-dir build/debug-system -R gentest_core_parse_validate --output-on-failure
```
Failure snippet:
```text
FAIL: template regression: template argument count
FAIL: template regression: template type preserved
FAIL: parameters_pack regression: template commas must not trigger arity mismatch
FAIL: parameters_pack regression: pack parsed
Total failures: 4
```

### 3) Namespace suite-attribute back-scan with trailing comment (TRUE)
Command:
```bash
ctest --test-dir build/debug-system -R gentest_codegen_namespace_suite_attr_with_trailing_comment --output-on-failure
```
Failure snippet:
```text
Expected substring not found in file:
'suite_override/comment_backscan/trailing_comment_suite_attr_case'
...
.name = "smoke/suite_comment_backscan/trailing_comment_suite_attr_case",
.suite = "smoke/suite_comment_backscan"
```
Interpretation: suite override attribute was not applied when a trailing comment appears between the namespace attribute and namespace name.

### 4) `parse_tu_index` register symbol collision (TRUE)
Command:
```bash
ctest --test-dir build/debug-system -R gentest_tu_register_symbol_collision --output-on-failure
```
Failure snippet:
```text
register symbol collision detected for TU outputs. Symbols:
register_tu_0042;register_tu_0042
```
Interpretation: two distinct TU inputs produced the same registration symbol via `parse_tu_index`.

## Added Regression Tests (Red)
- `tools/core_tests/parse_validate_tests.cpp`
  - Template-argument splitting regression (`template(T, std::pair<int, int>)`).
  - `parameters_pack` row tuple regression with `std::pair<int, int>{...}` entries.
- `tests/smoke/namespace_suite_comment.cpp`
- `tests/CMakeLists.txt`
  - `gentest_codegen_namespace_suite_attr_with_trailing_comment`
  - `gentest_tu_register_symbol_collision`
- `cmake/CheckTuRegisterSymbolCollision.cmake`
- `tests/cmake/tu_register_symbol_collision/CMakeLists.txt`

## False/Irrelevant Findings
None in this set. All four findings are reproducible.
