# Sanitizer and Coverage Verification (API/Context)

Date: 2026-03-04
Repo: `/home/ai-dev1/repos/gentest/.wt/api-context`

## Scope
Touched API/context proofs:
- `tests/proofs/api_context/adopt_copy_contract_red.cpp`
- `tests/proofs/api_context/clear_logs_buffered_regression.cpp`
- `tests/proofs/api_context/event_chronology_red.cpp`
- `tests/proofs/api_context/expect_throw_std_exception_red.cpp`

Note: these proofs are not CTest-registered in this branch, so checks were run directly with the corresponding preset instrumentation flags.

## 1) Sanitizer checks (`alusan` preset)

### Configure
```bash
cmake --preset=alusan
```
Result: configure succeeded.

### Compile touched proofs with `alusan` flags
```bash
mkdir -p build/alusan/proofs/api_context
for src in tests/proofs/api_context/*.cpp; do
  name="$(basename "${src%.cpp}")"
  /usr/lib64/ccache/c++ -std=c++20 -Iinclude \
    -fsanitize=address,leak,undefined -g -O1 -fno-omit-frame-pointer -pthread \
    "$src" -o "build/alusan/proofs/api_context/$name"
done
```
Result: all 4 proof binaries compiled.

Observed compile warnings (no build failure):
- `include/gentest/runner.h`: `-Wexceptions` for `expect_throw` / `require_throw` instantiations with `Expected=std::exception` (caught by earlier handler).

### Execute sanitizer-instrumented proofs
```bash
export ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:print_stats=1:check_initialization_order=1:strict_init_order=1'
export LSAN_OPTIONS='print_suppressions=false:report_objects=1'
export UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1:report_error_type=1'
for exe in build/alusan/proofs/api_context/*; do
  echo "==> $exe"
  "$exe"
done
```
Result: all 4 passed.

Runtime output highlights:
- `clear_logs_buffered_regression`: `PASS: clear_logs clears buffered logs while preserving failures`
- `event_chronology_red`: `PASS: chronology preserved (failure before later log)`
- `expect_throw_std_exception_red`: `PASS: framework exceptions are rethrown even when Expected=std::exception`

## 2) Coverage checks (`coverage` preset)

### Configure
```bash
cmake --preset=coverage
```
Result: configure succeeded.

### Compile touched proofs with coverage flags
```bash
mkdir -p build/coverage/proofs/api_context
for src in tests/proofs/api_context/*.cpp; do
  name="$(basename "${src%.cpp}")"
  /usr/lib64/ccache/c++ -std=c++20 -Iinclude -g -O0 --coverage -pthread \
    "$src" -o "build/coverage/proofs/api_context/$name"
done
```
Result: all 4 proof binaries compiled.

Observed compile warnings (no build failure):
- same `-Wexceptions` warnings in `include/gentest/runner.h` for `Expected=std::exception` instantiations.

### Execute coverage-instrumented proofs
```bash
for exe in \
  build/coverage/proofs/api_context/adopt_copy_contract_red \
  build/coverage/proofs/api_context/clear_logs_buffered_regression \
  build/coverage/proofs/api_context/event_chronology_red \
  build/coverage/proofs/api_context/expect_throw_std_exception_red; do
  echo "==> $exe"
  "$exe"
done
```
Result: all 4 passed.

### Coverage artifacts check
```bash
find build/coverage/proofs/api_context -maxdepth 1 -type f -name '*.gcno' | sort
find build/coverage/proofs/api_context -maxdepth 1 -type f -name '*.gcda' | sort
```
Result: `.gcno` and `.gcda` were generated for all 4 touched proofs.
