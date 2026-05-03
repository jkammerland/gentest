# Requires:
#  -DPROG=<path to executable>
# Optional:
#  -DARGS=<optional CLI args>
#  -DTIMEOUT_SEC=<seconds>
#  -DEXPECT_RC=<expected numeric exit code or NONZERO>
#  -DREQUIRED_SUBSTRING=<substring that must be present in combined output>
#  -DREQUIRED_SUBSTRINGS=<substrings delimited by '|' that must be present in combined output>
#  -DEXPECT_COUNT_SUBSTRING=<substring whose exact occurrence count is enforced>
#  -DEXPECT_COUNT=<expected count for EXPECT_COUNT_SUBSTRING>
#  -DFORBID_SUBSTRING=<substring that must NOT be present in combined output>
#  -DFORBID_SUBSTRINGS=<substrings delimited by '|' that must NOT be present in combined output>

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckNoSubstring.cmake: PROG not set")
endif()

set(_emu)
if(DEFINED EMU)
  if(EMU MATCHES ";")
    set(_emu ${EMU})
  else()
    separate_arguments(_emu NATIVE_COMMAND "${EMU}")
  endif()
endif()

set(_args)
if(DEFINED ARGS)
  if(ARGS MATCHES ";")
    set(_args ${ARGS})
  else()
    separate_arguments(_args NATIVE_COMMAND "${ARGS}")
  endif()
endif()

set(_timeout_args)
if(DEFINED TIMEOUT_SEC AND NOT "${TIMEOUT_SEC}" STREQUAL "")
  set(_timeout_args TIMEOUT "${TIMEOUT_SEC}")
endif()

execute_process(
  COMMAND ${_emu} "${PROG}" ${_args}
  ${_timeout_args}
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

set(_all "${out}\n${err}")

function(_gentest_result_is_process_failure out_var value)
  if("${value}" MATCHES "^-?[0-9]+$")
    if("${value}" STREQUAL "0")
      set(${out_var} FALSE PARENT_SCOPE)
    else()
      set(${out_var} TRUE PARENT_SCOPE)
    endif()
  elseif("${value}" MATCHES "^Subprocess (aborted|terminated)" OR "${value}" MATCHES "^Exit code 0x[0-9A-Fa-f]+")
    set(${out_var} TRUE PARENT_SCOPE)
  else()
    set(${out_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

if(DEFINED TIMEOUT_SEC AND NOT "${TIMEOUT_SEC}" STREQUAL "" AND "${rc}" MATCHES "[Tt]imeout")
  message(FATAL_ERROR "Process did not complete within ${TIMEOUT_SEC}s (rc='${rc}'). Output:\n${_all}")
endif()

if(DEFINED EXPECT_RC AND NOT "${EXPECT_RC}" STREQUAL "")
  if("${EXPECT_RC}" STREQUAL "NONZERO")
    _gentest_result_is_process_failure(_gentest_rc_is_process_failure "${rc}")
    if(NOT _gentest_rc_is_process_failure)
      message(FATAL_ERROR "Expected non-zero process exit, but process did not return an exit/termination code (rc='${rc}'). Output:\n${_all}")
    endif()
  elseif(NOT "${rc}" MATCHES "^-?[0-9]+$")
    message(FATAL_ERROR "Expected exit code ${EXPECT_RC}, but process did not return a numeric exit code (rc='${rc}'). Output:\n${_all}")
  elseif(NOT rc EQUAL EXPECT_RC)
    message(FATAL_ERROR "Expected exit code ${EXPECT_RC}, got ${rc}. Output:\n${_all}")
  endif()
endif()

if(DEFINED REQUIRED_SUBSTRING AND NOT "${REQUIRED_SUBSTRING}" STREQUAL "")
  string(FIND "${_all}" "${REQUIRED_SUBSTRING}" _expect_pos)
  if(_expect_pos EQUAL -1)
    message(FATAL_ERROR "Expected substring not found: '${REQUIRED_SUBSTRING}'. Output:\n${_all}")
  endif()
endif()

if(DEFINED REQUIRED_SUBSTRINGS AND NOT "${REQUIRED_SUBSTRINGS}" STREQUAL "")
  set(_required_values "${REQUIRED_SUBSTRINGS}")
  string(REPLACE "|" ";" _required_values "${_required_values}")
  foreach(_required IN LISTS _required_values)
    string(FIND "${_all}" "${_required}" _expect_pos)
    if(_expect_pos EQUAL -1)
      message(FATAL_ERROR "Expected substring not found: '${_required}'. Output:\n${_all}")
    endif()
  endforeach()
endif()

if(DEFINED EXPECT_COUNT_SUBSTRING AND NOT "${EXPECT_COUNT_SUBSTRING}" STREQUAL "")
  if(NOT DEFINED EXPECT_COUNT OR "${EXPECT_COUNT}" STREQUAL "")
    message(FATAL_ERROR "EXPECT_COUNT must be set when EXPECT_COUNT_SUBSTRING is used")
  endif()
  string(LENGTH "${EXPECT_COUNT_SUBSTRING}" _needle_len)
  if(_needle_len EQUAL 0)
    message(FATAL_ERROR "EXPECT_COUNT_SUBSTRING must not be empty")
  endif()
  string(LENGTH "${_all}" _all_len)
  string(REPLACE "${EXPECT_COUNT_SUBSTRING}" "" _without_count_substring "${_all}")
  string(LENGTH "${_without_count_substring}" _without_len)
  math(EXPR _count "(${_all_len} - ${_without_len}) / ${_needle_len}")
  if(NOT _count EQUAL EXPECT_COUNT)
    message(FATAL_ERROR "Expected substring '${EXPECT_COUNT_SUBSTRING}' exactly ${EXPECT_COUNT} times, found ${_count}. Output:\n${_all}")
  endif()
endif()

set(_forbidden_substrings)
if(DEFINED FORBID_SUBSTRINGS AND NOT "${FORBID_SUBSTRINGS}" STREQUAL "")
  set(_forbid_values "${FORBID_SUBSTRINGS}")
  string(REPLACE "|" ";" _forbid_values "${_forbid_values}")
  list(APPEND _forbidden_substrings ${_forbid_values})
endif()
if(DEFINED FORBID_SUBSTRING AND NOT "${FORBID_SUBSTRING}" STREQUAL "")
  list(APPEND _forbidden_substrings "${FORBID_SUBSTRING}")
endif()

foreach(_forbid IN LISTS _forbidden_substrings)
  string(FIND "${_all}" "${_forbid}" _forbid_pos)
  if(NOT _forbid_pos EQUAL -1)
    message(FATAL_ERROR "Forbidden substring found: '${_forbid}'. Output:\n${_all}")
  endif()
endforeach()
