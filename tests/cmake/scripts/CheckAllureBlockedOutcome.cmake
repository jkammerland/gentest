# Requires:
#  -DPROG=<path to executable>
#  -DOUT_DIR=<Allure output directory>
# Optional:
#  -DARGS=<optional CLI args>
#  -DEXPECT_RC=<expected numeric exit code>
#  -DEXPECTED_REASON=<blocked reason without the "blocked:" prefix>

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckAllureBlockedOutcome.cmake: PROG not set")
endif()
if(NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "CheckAllureBlockedOutcome.cmake: OUT_DIR not set")
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

if(NOT DEFINED EXPECT_RC OR "${EXPECT_RC}" STREQUAL "")
  set(EXPECT_RC 1)
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
execute_process(
  COMMAND ${_emu} "${PROG}" ${_args} --allure-dir=${OUT_DIR}
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT rc EQUAL EXPECT_RC)
  message(FATAL_ERROR "Expected exit code ${EXPECT_RC}, got ${rc}. Output:\n${out}\nErrors:\n${err}")
endif()

file(GLOB _result_files "${OUT_DIR}/*-result.json")
list(LENGTH _result_files _result_count)
if(_result_count EQUAL 0)
  message(FATAL_ERROR "Expected at least one Allure result file in ${OUT_DIR}")
endif()

set(_blocked_result_json "")
foreach(_result_file IN LISTS _result_files)
  file(READ "${_result_file}" _candidate_json)
  string(FIND "${_candidate_json}" "\"status\":\"skipped\"" _skipped_pos)
  string(FIND "${_candidate_json}" "\"name\":\"blocked\"" _blocked_label_pos)
  if(NOT _skipped_pos EQUAL -1 AND NOT _blocked_label_pos EQUAL -1)
    if(NOT _blocked_result_json STREQUAL "")
      message(FATAL_ERROR "Expected one blocked Allure result, found multiple in ${OUT_DIR}")
    endif()
    set(_blocked_result_json "${_candidate_json}")
  endif()
endforeach()

if(_blocked_result_json STREQUAL "")
  message(FATAL_ERROR "Expected a skipped Allure result with a blocked label in ${OUT_DIR}")
endif()

foreach(_required IN ITEMS
    "\"status\":\"skipped\""
    "\"name\":\"blocked\"")
  string(FIND "${_blocked_result_json}" "${_required}" _required_pos)
  if(_required_pos EQUAL -1)
    message(FATAL_ERROR "Expected Allure blocked result to contain '${_required}'.\n${_blocked_result_json}")
  endif()
endforeach()

if(DEFINED EXPECTED_REASON AND NOT "${EXPECTED_REASON}" STREQUAL "")
  foreach(_required_reason IN ITEMS
      "\"message\":\"blocked: ${EXPECTED_REASON}\""
      "\"value\":\"${EXPECTED_REASON}\"")
    string(FIND "${_blocked_result_json}" "${_required_reason}" _reason_pos)
    if(_reason_pos EQUAL -1)
      message(FATAL_ERROR "Expected Allure blocked result to contain '${_required_reason}'.\n${_blocked_result_json}")
    endif()
  endforeach()
endif()
