# Requires:
#  -DPROG=<path to test binary>

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckTimeUnitTables.cmake: PROG not set")
endif()

if(POLICY CMP0007)
  cmake_policy(SET CMP0007 NEW)
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

function(_extract_row out_row text row_name)
  string(REPLACE "\r\n" "\n" _normalized "${text}")
  string(REPLACE "\r" "\n" _normalized "${_normalized}")
  string(REPLACE "\n" ";" _lines "${_normalized}")
  set(_row)
  foreach(_line IN LISTS _lines)
    if(_line MATCHES "^\\|[ ]*${row_name}[ ]*\\|")
      set(_row "${_line}")
      break()
    endif()
  endforeach()
  if(_row STREQUAL "")
    message(FATAL_ERROR "Could not find table row for '${row_name}'. Output:\n${text}")
  endif()
  set(${out_row} "${_row}" PARENT_SCOPE)
endfunction()

function(_assert_row_cell_regex row cell_index pattern label)
  string(REPLACE "|" ";" _cells "${row}")
  list(LENGTH _cells _len)
  if(_len LESS_EQUAL ${cell_index})
    message(FATAL_ERROR "Missing column ${cell_index} for ${label}. Row:\n${row}")
  endif()
  list(GET _cells ${cell_index} _cell)
  string(STRIP "${_cell}" _cell)
  if(NOT _cell MATCHES "${pattern}")
    message(FATAL_ERROR "Unexpected value for ${label}: '${_cell}' does not match '${pattern}'. Row:\n${row}")
  endif()
endfunction()

function(_collect_hist_ranges text out_ranges)
  string(REPLACE "\r\n" "\n" _normalized "${text}")
  string(REPLACE "\r" "\n" _normalized "${_normalized}")
  string(REPLACE "\n" ";" _lines "${_normalized}")

  set(_ranges)
  set(_in_hist FALSE)
  foreach(_line IN LISTS _lines)
    if(_line MATCHES "^Jitter histogram \\(")
      set(_in_hist TRUE)
      continue()
    endif()
    if(NOT _in_hist)
      continue()
    endif()

    if(_line MATCHES "^\\|[ ]*[0-9]+[ ]*\\|")
      string(REPLACE "|" ";" _cells "${_line}")
      list(LENGTH _cells _len)
      if(_len GREATER 2)
        list(GET _cells 2 _range)
        string(STRIP "${_range}" _range)
        if(NOT _range STREQUAL "")
          list(APPEND _ranges "${_range}")
        endif()
      endif()
      continue()
    endif()

    if(_line STREQUAL "" AND NOT _ranges STREQUAL "")
      break()
    endif()
  endforeach()

  set(${out_ranges} "${_ranges}" PARENT_SCOPE)
endfunction()

_run(
  _bench_out
  --run=regressions/bench_sleep_ms
  --kind=bench
  --bench-min-epoch-time-s=0.001
  --bench-epochs=2
  --bench-warmup=0
  --bench-max-total-time-s=0.02)

string(FIND "${_bench_out}" "Median (ms/op)" _bench_header_pos)
if(_bench_header_pos EQUAL -1)
  message(FATAL_ERROR "Expected scaled bench header 'Median (ms/op)'. Output:\n${_bench_out}")
endif()

_extract_row(_bench_row "${_bench_out}" "regressions/bench_sleep_ms")
foreach(_col IN ITEMS 4 5 6 7 8 9)
  _assert_row_cell_regex("${_bench_row}" ${_col} "^[0-9]+\\.[0-9][0-9][0-9]$" "bench column ${_col}")
endforeach()

_run(
  _jitter_out
  --run=regressions/jitter_sleep_ms
  --kind=jitter
  --bench-min-epoch-time-s=0.001
  --bench-epochs=2
  --bench-warmup=0
  --bench-max-total-time-s=0.02
  --jitter-bins=5)

string(FIND "${_jitter_out}" "Median (ms/op)" _jitter_header_pos)
if(_jitter_header_pos EQUAL -1)
  message(FATAL_ERROR "Expected scaled jitter header 'Median (ms/op)'. Output:\n${_jitter_out}")
endif()

_extract_row(_jitter_row "${_jitter_out}" "regressions/jitter_sleep_ms")
foreach(_col IN ITEMS 3 4 6 7 8 9 10)
  _assert_row_cell_regex("${_jitter_row}" ${_col} "^[0-9]+\\.[0-9][0-9][0-9]$" "jitter column ${_col}")
endforeach()

_collect_hist_ranges("${_jitter_out}" _ranges)
if(_ranges STREQUAL "")
  message(FATAL_ERROR "No histogram ranges were parsed from jitter output:\n${_jitter_out}")
endif()

set(_seen_ranges)
foreach(_range IN LISTS _ranges)
  list(FIND _seen_ranges "${_range}" _dup_idx)
  if(NOT _dup_idx EQUAL -1)
    message(FATAL_ERROR "Duplicate histogram display range '${_range}' found. Output:\n${_jitter_out}")
  endif()
  list(APPEND _seen_ranges "${_range}")
endforeach()

message(STATUS "Time-unit scaling table checks passed")
