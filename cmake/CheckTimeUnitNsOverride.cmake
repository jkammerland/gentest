# Requires:
#  -DPROG=<path to test binary>

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckTimeUnitNsOverride.cmake: PROG not set")
endif()

set(_emu)
if(DEFINED EMU)
  if(EMU MATCHES ";")
    set(_emu ${EMU})
  else()
    separate_arguments(_emu NATIVE_COMMAND "${EMU}")
  endif()
endif()

function(_run out_var)
  execute_process(
    COMMAND ${_emu} "${PROG}" ${ARGN}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Command failed with code ${_rc}: ${ARGN}\nOutput:\n${_out}\nErrors:\n${_err}")
  endif()
  set(${out_var} "${_out}\n${_err}" PARENT_SCOPE)
endfunction()

function(_require_contains text needle)
  string(FIND "${text}" "${needle}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "Expected substring not found: '${needle}'. Output:\n${text}")
  endif()
endfunction()

function(_require_not_contains text needle)
  string(FIND "${text}" "${needle}" _pos)
  if(NOT _pos EQUAL -1)
    message(FATAL_ERROR "Unexpected substring present: '${needle}'. Output:\n${text}")
  endif()
endfunction()

_run(
  _bench_out
  --run=regressions/bench_sleep_ms
  --kind=bench
  --time-unit=ns
  --bench-min-epoch-time-s=0.001
  --bench-epochs=2
  --bench-warmup=0
  --bench-max-total-time-s=0.02)

_require_contains("${_bench_out}" "Median (ns/op)")
_require_contains("${_bench_out}" "Total (ns)")
_require_not_contains("${_bench_out}" "(us/op)")
_require_not_contains("${_bench_out}" "(ms/op)")
_require_not_contains("${_bench_out}" "(s/op)")

_run(
  _jitter_out
  --run=regressions/jitter_sleep_ms
  --kind=jitter
  --time-unit=ns
  --bench-min-epoch-time-s=0.001
  --bench-epochs=2
  --bench-warmup=0
  --bench-max-total-time-s=0.02
  --jitter-bins=5)

_require_contains("${_jitter_out}" "Median (ns/op)")
_require_contains("${_jitter_out}" "Range (ns/op)")
_require_not_contains("${_jitter_out}" "(us/op)")
_require_not_contains("${_jitter_out}" "(ms/op)")
_require_not_contains("${_jitter_out}" "(s/op)")

message(STATUS "Forced ns unit checks passed")
