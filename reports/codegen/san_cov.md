# Sanitizer and Coverage Verification

Date: 2026-03-04
Workspace: `/home/ai-dev1/repos/gentest/.wt/codegen-parser`
Scope: affected codegen/core checks (targeted subset)

## Targeted Test Filter

`^(gentest_core_parse_validate|gentest_codegen_.*|gentest_tu_register_symbol_collision)$`

## Sanitizer (`alusan`) Run

1. `cmake --preset=alusan`
2. `cmake --build --preset=alusan --target gentest_codegen gentest_core_tests`
3. `ctest --preset=alusan -R '^(gentest_core_parse_validate|gentest_codegen_.*|gentest_tu_register_symbol_collision)$'`

Result:
- Passed: 30/30
- Failed: 0
- Real test time: 17.51 sec

## Coverage (`coverage`) Run

1. `cmake --preset=coverage`
2. `cmake --build --preset=coverage --target gentest_codegen gentest_core_tests`
3. `ctest --preset=coverage -R '^(gentest_core_parse_validate|gentest_codegen_.*|gentest_tu_register_symbol_collision)$'`

Result:
- Passed: 30/30
- Failed: 0
- Real test time: 10.75 sec

## Notes

- This is a focused subset run for changed parser/codegen/core areas, as requested.
- Included new/affected checks such as:
  - `gentest_codegen_namespace_suite_attr_with_trailing_comment`
  - `gentest_tu_register_symbol_collision`
  - `gentest_core_parse_validate`
