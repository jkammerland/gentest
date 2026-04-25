# Usage:
#   cmake -DPROG=<path> -DPASS=<n> -DFAIL=<n> -DSKIP=<n> -P tests/cmake/scripts/CheckTestInventory.cmake
#   cmake -DPROG=<path> -DPASS=<n> -DFAIL=<n> -DSKIP=<n> [-DBLOCKED=<n>] [-DXFAIL=<n>] [-DXPASS=<n>] [-DEXPECT_RC=<n>] -P tests/cmake/scripts/CheckTestInventory.cmake
#   cmake -DPROG=<path> [-DCASES=<n>] [-DEXPECTED_LIST_FILE=<path>] [-DPASS=<n> -DFAIL=<n> -DSKIP=<n>] [-DBLOCKED=<n>] [-DXFAIL=<n>] [-DXPASS=<n>] [-DEXPECT_RC=<n>] -P tests/cmake/scripts/CheckTestInventory.cmake
#
# EXPECTED_LIST_FILE may use either plain case names (legacy, implicitly PASS)
# or explicit status markers:
#   [PASS] suite/test_name
#   [SKIP] suite/other_test
#   [BLOCKED] suite/blocked_by_infrastructure
#   [FAIL] suite/failing_test
#   [XFAIL] suite/expected_failure
#   [XPASS] suite/unexpected_pass

if(NOT DEFINED PROG)
  message(FATAL_ERROR "PROG not set")
endif()

set(_emu)
if(DEFINED EMU)
  if(EMU MATCHES ";")
    set(_emu ${EMU}) # already a list
  else()
    separate_arguments(_emu NATIVE_COMMAND "${EMU}") # string
  endif()
endif()

set(_args)
if(DEFINED ARGS)
  if(ARGS MATCHES ";")
    set(_args ${ARGS}) # already a list
  else()
    separate_arguments(_args NATIVE_COMMAND "${ARGS}") # string
  endif()
endif()

function(_gentest_count_output_lines text out_var)
  if("${text}" STREQUAL "")
    set(${out_var} 0 PARENT_SCOPE)
    return()
  endif()

  string(REGEX MATCHALL "\n" _lines "${text}")
  list(LENGTH _lines _line_count)
  if(NOT text MATCHES "\n$")
    math(EXPR _line_count "${_line_count} + 1")
  endif()
  set(${out_var} "${_line_count}" PARENT_SCOPE)
endfunction()

function(_gentest_normalize_text text out_var)
  string(ASCII 27 _esc)
  string(REPLACE "\r" "" _normalized "${text}")
  string(REGEX REPLACE "${_esc}\\[[0-9;:;?]*[A-Za-z]" "" _normalized "${_normalized}")
  string(REGEX REPLACE "\n+$" "" _normalized "${_normalized}")
  set(${out_var} "${_normalized}" PARENT_SCOPE)
endfunction()

function(_gentest_escape_list_item text out_var)
  string(REPLACE ";" "\\;" _escaped "${text}")
  set(${out_var} "${_escaped}" PARENT_SCOPE)
endfunction()

function(_gentest_normalize_sorted_lines text out_var)
  _gentest_normalize_text("${text}" _normalized)
  if("${_normalized}" STREQUAL "")
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()

  set(_filtered)
  set(_remaining "${_normalized}")
  while(NOT "${_remaining}" STREQUAL "")
    string(REGEX MATCH "^([^\n]*)(\n|$)" _entry "${_remaining}")
    set(_line "${CMAKE_MATCH_1}")
    string(LENGTH "${_entry}" _entry_len)
    string(SUBSTRING "${_remaining}" ${_entry_len} -1 _remaining)
    if(NOT _line STREQUAL "")
      _gentest_escape_list_item("${_line}" _escaped_line)
      list(APPEND _filtered "${_escaped_line}")
    endif()
  endwhile()
  list(SORT _filtered)
  set(${out_var} "${_filtered}" PARENT_SCOPE)
endfunction()

function(_gentest_parse_expected_list_text text_var out_names_var out_cases_var out_pass_var out_fail_var out_skip_var out_blocked_var out_xfail_var out_xpass_var)
  _gentest_normalize_text("${${text_var}}" _remaining)
  if("${_remaining}" STREQUAL "")
    set(${out_names_var} "" PARENT_SCOPE)
    set(${out_cases_var} 0 PARENT_SCOPE)
    set(${out_pass_var} 0 PARENT_SCOPE)
    set(${out_fail_var} 0 PARENT_SCOPE)
    set(${out_skip_var} 0 PARENT_SCOPE)
    set(${out_blocked_var} 0 PARENT_SCOPE)
    set(${out_xfail_var} 0 PARENT_SCOPE)
    set(${out_xpass_var} 0 PARENT_SCOPE)
    return()
  endif()

  set(_names)
  set(_cases 0)
  set(_pass 0)
  set(_fail 0)
  set(_skip 0)
  set(_blocked 0)
  set(_xfail 0)
  set(_xpass 0)

  while(NOT "${_remaining}" STREQUAL "")
    string(REGEX MATCH "^([^\n]*)(\n|$)" _entry "${_remaining}")
    set(_line "${CMAKE_MATCH_1}")
    string(LENGTH "${_entry}" _entry_len)
    string(SUBSTRING "${_remaining}" ${_entry_len} -1 _remaining)

    if(_line STREQUAL "")
      continue()
    endif()

    set(_status "PASS")
    set(_name "${_line}")
    if(_line MATCHES "^[ \t]*\\[(PASS|FAIL|SKIP|BLOCKED|XFAIL|XPASS)\\][ \t]+(.+)$")
      set(_status "${CMAKE_MATCH_1}")
      set(_name "${CMAKE_MATCH_2}")
    endif()

    if(_name STREQUAL "")
      message(FATAL_ERROR "Expected list entry is missing a case name: '${_line}'")
    endif()

    _gentest_escape_list_item("${_name}" _escaped_name)
    list(APPEND _names "${_escaped_name}")
    math(EXPR _cases "${_cases} + 1")
    if(_status STREQUAL "PASS")
      math(EXPR _pass "${_pass} + 1")
    elseif(_status STREQUAL "FAIL")
      math(EXPR _fail "${_fail} + 1")
    elseif(_status STREQUAL "SKIP")
      math(EXPR _skip "${_skip} + 1")
    elseif(_status STREQUAL "BLOCKED")
      math(EXPR _blocked "${_blocked} + 1")
    elseif(_status STREQUAL "XFAIL")
      math(EXPR _xfail "${_xfail} + 1")
    elseif(_status STREQUAL "XPASS")
      math(EXPR _xpass "${_xpass} + 1")
    else()
      message(FATAL_ERROR "Unsupported expected list status '${_status}' in '${_line}'")
    endif()
  endwhile()

  list(SORT _names)
  set(${out_names_var} "${_names}" PARENT_SCOPE)
  set(${out_cases_var} "${_cases}" PARENT_SCOPE)
  set(${out_pass_var} "${_pass}" PARENT_SCOPE)
  set(${out_fail_var} "${_fail}" PARENT_SCOPE)
  set(${out_skip_var} "${_skip}" PARENT_SCOPE)
  set(${out_blocked_var} "${_blocked}" PARENT_SCOPE)
  set(${out_xfail_var} "${_xfail}" PARENT_SCOPE)
  set(${out_xpass_var} "${_xpass}" PARENT_SCOPE)
endfunction()

function(_gentest_count_status_lines text status out_var)
  set(_search_text "\n${text}")
  string(REGEX MATCHALL "\n[ \t]*\\[ ${status} \\]" _matches "${_search_text}")
  list(LENGTH _matches _count)
  set(${out_var} "${_count}" PARENT_SCOPE)
endfunction()

function(_gentest_run_and_capture name out_var err_var rc_var)
  execute_process(
    COMMAND ${_emu} "${PROG}" ${_args} ${ARGN}
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    RESULT_VARIABLE _rc)

  set(${out_var} "${_out}" PARENT_SCOPE)
  set(${err_var} "${_err}" PARENT_SCOPE)
  set(${rc_var} "${_rc}" PARENT_SCOPE)
endfunction()

function(_gentest_check_list_mode mode_name expected_cases)
  _gentest_run_and_capture("${mode_name}" _out _err _rc ${ARGN})
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "${mode_name} failed with code ${_rc}. Output:\n${_out}\nErrors:\n${_err}")
  endif()

  _gentest_count_output_lines("${_out}" _line_count)
  if(NOT _line_count EQUAL expected_cases)
    message(FATAL_ERROR "Expected ${expected_cases} ${mode_name} lines, got ${_line_count}. Output:\n${_out}")
  endif()
endfunction()

set(_normalized_expected_list)
set(_expected_cases)
set(_derived_pass)
set(_derived_fail)
set(_derived_skip)
set(_derived_blocked)
set(_derived_xfail)
set(_derived_xpass)
set(_have_inventory_expectations OFF)
set(_check_list_tests ON)

if(DEFINED CHECK_LIST_TESTS AND NOT CHECK_LIST_TESTS)
  set(_check_list_tests OFF)
endif()

if(DEFINED CASES AND NOT "${CASES}" STREQUAL "")
  set(_expected_cases "${CASES}")
  set(_have_inventory_expectations ON)
endif()

if(DEFINED EXPECTED_LIST_FILE AND NOT "${EXPECTED_LIST_FILE}" STREQUAL "")
  if(NOT EXISTS "${EXPECTED_LIST_FILE}")
    message(FATAL_ERROR "Expected list file not found: ${EXPECTED_LIST_FILE}")
  endif()
  file(READ "${EXPECTED_LIST_FILE}" _expected_list_text)
  _gentest_parse_expected_list_text(_expected_list_text
    _normalized_expected_list
    _derived_expected_cases
    _derived_pass
    _derived_fail
    _derived_skip
    _derived_blocked
    _derived_xfail
    _derived_xpass)
  if(NOT _have_inventory_expectations)
    set(_expected_cases "${_derived_expected_cases}")
    set(_have_inventory_expectations ON)
  endif()
endif()

if(_have_inventory_expectations)
  _gentest_check_list_mode("--list" "${_expected_cases}" --list)
endif()

if(_have_inventory_expectations AND _check_list_tests)
  _gentest_check_list_mode("--list-tests" "${_expected_cases}" --list-tests)
endif()

if(_have_inventory_expectations
   AND _check_list_tests
   AND DEFINED EXPECTED_LIST_FILE
   AND NOT "${EXPECTED_LIST_FILE}" STREQUAL "")
  _gentest_run_and_capture("--list-tests exact" _list_tests_out _list_tests_err _list_tests_rc --list-tests)
  if(NOT _list_tests_rc EQUAL 0)
    message(FATAL_ERROR "--list-tests exact failed with code ${_list_tests_rc}. Output:\n${_list_tests_out}\nErrors:\n${_list_tests_err}")
  endif()
  _gentest_normalize_sorted_lines("${_list_tests_out}" _normalized_actual_list)
  if(NOT _normalized_actual_list STREQUAL _normalized_expected_list)
    string(JOIN "\n" _expected_block ${_normalized_expected_list})
    string(JOIN "\n" _actual_block ${_normalized_actual_list})
    message(FATAL_ERROR
      "Expected --list-tests membership from ${EXPECTED_LIST_FILE}, but it differed.\nExpected:\n${_expected_block}\nActual:\n${_actual_block}")
  endif()
endif()

if(NOT DEFINED PASS AND NOT DEFINED FAIL AND NOT DEFINED SKIP)
  if(DEFINED EXPECTED_LIST_FILE AND NOT "${EXPECTED_LIST_FILE}" STREQUAL "")
    set(PASS "${_derived_pass}")
    set(FAIL "${_derived_fail}")
    set(SKIP "${_derived_skip}")
    if(NOT _derived_blocked EQUAL 0)
      set(BLOCKED "${_derived_blocked}")
    endif()
    if(NOT _derived_xfail EQUAL 0)
      set(XFAIL "${_derived_xfail}")
    endif()
    if(NOT _derived_xpass EQUAL 0)
      set(XPASS "${_derived_xpass}")
    endif()
  endif()
endif()

if(NOT DEFINED PASS AND NOT DEFINED FAIL AND NOT DEFINED SKIP)
  if(_have_inventory_expectations)
    return()
  endif()
  message(FATAL_ERROR "CASES, EXPECTED_LIST_FILE, or PASS/FAIL/SKIP must be set")
endif()

if(NOT DEFINED PASS OR NOT DEFINED FAIL OR NOT DEFINED SKIP)
  message(FATAL_ERROR "PASS, FAIL, and SKIP must either all be set or all be omitted")
endif()

_gentest_run_and_capture("default run" out err rc)

_gentest_normalize_text("${out}${err}" all)
_gentest_count_status_lines("${all}" "PASS" pass_count)
_gentest_count_status_lines("${all}" "FAIL" fail_count)
_gentest_count_status_lines("${all}" "SKIP" skip_count)
_gentest_count_status_lines("${all}" "BLOCKED" blocked_count)
_gentest_count_status_lines("${all}" "XFAIL" xfail_count)
_gentest_count_status_lines("${all}" "XPASS" xpass_count)

set(_ok TRUE)
if(NOT pass_count EQUAL PASS)
  message(STATUS "PASS count mismatch: expected ${PASS}, got ${pass_count}")
  set(_ok FALSE)
endif()
if(NOT fail_count EQUAL FAIL)
  message(STATUS "FAIL count mismatch: expected ${FAIL}, got ${fail_count}")
  set(_ok FALSE)
endif()
if(NOT skip_count EQUAL SKIP)
  message(STATUS "SKIP count mismatch: expected ${SKIP}, got ${skip_count}")
  set(_ok FALSE)
endif()
if(DEFINED BLOCKED AND NOT "${BLOCKED}" STREQUAL "" AND NOT blocked_count EQUAL BLOCKED)
  message(STATUS "BLOCKED count mismatch: expected ${BLOCKED}, got ${blocked_count}")
  set(_ok FALSE)
endif()
if(DEFINED XFAIL AND NOT "${XFAIL}" STREQUAL "" AND NOT xfail_count EQUAL XFAIL)
  message(STATUS "XFAIL count mismatch: expected ${XFAIL}, got ${xfail_count}")
  set(_ok FALSE)
endif()
if(DEFINED XPASS AND NOT "${XPASS}" STREQUAL "" AND NOT xpass_count EQUAL XPASS)
  message(STATUS "XPASS count mismatch: expected ${XPASS}, got ${xpass_count}")
  set(_ok FALSE)
endif()

if(DEFINED EXPECT_RC AND NOT "${EXPECT_RC}" STREQUAL "")
  if(NOT rc EQUAL EXPECT_RC)
    message(STATUS "Exit code mismatch: expected ${EXPECT_RC}, got ${rc}")
    set(_ok FALSE)
  endif()
else()
  set(_fail_like ${FAIL})
  if(DEFINED XPASS AND NOT "${XPASS}" STREQUAL "")
    math(EXPR _fail_like "${_fail_like} + ${XPASS}")
  endif()
  if(DEFINED BLOCKED AND NOT "${BLOCKED}" STREQUAL "")
    math(EXPR _fail_like "${_fail_like} + ${BLOCKED}")
  endif()

  if(_fail_like GREATER 0)
    if(rc EQUAL 0)
      message(STATUS "Expected non-zero exit code when failures present, got ${rc}")
      set(_ok FALSE)
    endif()
  else()
    if(NOT rc EQUAL 0)
      message(STATUS "Unexpected non-zero exit code: ${rc}")
      set(_ok FALSE)
    endif()
  endif()
endif()

if(NOT _ok)
  message(FATAL_ERROR "Inventory check failed. Output:\n${out}\nErrors:\n${err}")
endif()
