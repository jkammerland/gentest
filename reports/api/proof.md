# API Context Semantics Proof (Red Phase)

Branch: `fix/api-context-semantics`  
Date: `2026-03-04`

## Summary

1. `gentest::ctx::Adopt` copy/copy-assign token accounting hazard: **TRUE**
2. `expect_throw`/`require_throw` with `Expected=std::exception` rethrow path behavior: **TRUE (reproduced; may be intentional but currently surprising/undocumented)**
3. Event chronology mismatch (`thread_local` buffered failures vs immediate logs): **TRUE**

---

## 1) `gentest::ctx::Adopt` copy/copy-assign token accounting hazard

### Red regression proof

- Proof source: `tests/proofs/api_context/adopt_copy_contract_red.cpp`
- Command:

```bash
c++ -std=c++20 -Iinclude tests/proofs/api_context/adopt_copy_contract_red.cpp -o tests/proofs/api_context/bin/adopt_copy_contract_red
```

- Recorded RC: `build_rc=1` (`reports/api/evidence/adopt_copy_contract_red.rc`)
- Failing evidence (`reports/api/evidence/adopt_copy_contract_red.build.log`):

```text
error: static assertion failed: red-phase: gentest::ctx::Adopt must be non-copyable ...
error: static assertion failed: red-phase: gentest::ctx::Adopt must be non-copy-assignable ...
```

### Why this proves the issue

The compile-time contract test asserts `Adopt` should be non-copyable/non-copy-assignable to keep token increments/decrements balanced. It fails, confirming the API currently permits copying and copy-assignment.

---

## 2) `expect_throw`/`require_throw` surprising behavior for `Expected=std::exception`

### Red regression proof

- Proof source: `tests/proofs/api_context/expect_throw_std_exception_red.cpp`
- Commands:

```bash
c++ -std=c++20 -Iinclude tests/proofs/api_context/expect_throw_std_exception_red.cpp -o tests/proofs/api_context/bin/expect_throw_std_exception_red
tests/proofs/api_context/bin/expect_throw_std_exception_red
```

- Recorded RC: `build_rc=0 run_rc=1` (`reports/api/evidence/expect_throw_std_exception_red.rc`)
- Failing evidence (`reports/api/evidence/expect_throw_std_exception_red.run.log`):

```text
RED: expected std::exception to be accepted without rethrow
expect_throw_completed=0, expect_throw_rethrew=1
require_throw_completed=0, require_throw_rethrew=1
```

### Why this proves the issue

Even though `gentest::failure` derives from `std::exception`, both helpers rethrow it via internal `catch (const gentest::failure&)` path before `catch (const Expected&)` when `Expected=std::exception`.

### Design rationale note

This may be intentional to avoid swallowing framework-internal failure/assertion flow. If that is the intended contract, it should be explicitly documented; otherwise behavior is surprising for users expecting normal C++ subtype matching.

---

## 3) Event chronology mismatch (buffered failures vs immediate logs)

### Red regression proof

- Proof source: `tests/proofs/api_context/event_chronology_red.cpp`
- Commands:

```bash
c++ -std=c++20 -Iinclude tests/proofs/api_context/event_chronology_red.cpp -o tests/proofs/api_context/bin/event_chronology_red
tests/proofs/api_context/bin/event_chronology_red
```

- Recorded RC: `build_rc=0 run_rc=1` (`reports/api/evidence/event_chronology_red.rc`)
- Failing evidence (`reports/api/evidence/event_chronology_red.run.log`):

```text
RED: chronology mismatch (log emitted before earlier failure)
fail_index=1, log_index=0
[0] kind=L line=L-after-failure
[1] kind=F line=F-before-log
```

### Why this proves the issue

`record_failure(...)` stores failure events in thread-local buffer, while `log(...)` appends immediately to context timeline. For a sequence `failure` then `log`, the persisted timeline is reordered as `log` then `failure`.

---

## Repro Artifacts

- `tests/proofs/api_context/adopt_copy_contract_red.cpp`
- `tests/proofs/api_context/expect_throw_std_exception_red.cpp`
- `tests/proofs/api_context/event_chronology_red.cpp`
- `reports/api/evidence/adopt_copy_contract_red.build.log`
- `reports/api/evidence/adopt_copy_contract_red.rc`
- `reports/api/evidence/expect_throw_std_exception_red.build.log`
- `reports/api/evidence/expect_throw_std_exception_red.run.log`
- `reports/api/evidence/expect_throw_std_exception_red.rc`
- `reports/api/evidence/event_chronology_red.build.log`
- `reports/api/evidence/event_chronology_red.run.log`
- `reports/api/evidence/event_chronology_red.rc`
