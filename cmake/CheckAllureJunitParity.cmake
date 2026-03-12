if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckAllureJunitParity.cmake: PROG not set")
endif()
if(NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "CheckAllureJunitParity.cmake: OUT_DIR not set")
endif()
if(NOT DEFINED JUNIT_PATH)
  message(FATAL_ERROR "CheckAllureJunitParity.cmake: JUNIT_PATH not set")
endif()
if(NOT DEFINED EXPECT_SUBSTRING)
  message(FATAL_ERROR "CheckAllureJunitParity.cmake: EXPECT_SUBSTRING not set")
endif()
if(NOT DEFINED EXPECT_CASE_NAME)
  message(FATAL_ERROR "CheckAllureJunitParity.cmake: EXPECT_CASE_NAME not set")
endif()
if(NOT DEFINED EXPECT_JUNIT_COUNTS)
  message(FATAL_ERROR "CheckAllureJunitParity.cmake: EXPECT_JUNIT_COUNTS not set")
endif()
if(NOT DEFINED EXPECT_RC)
  set(EXPECT_RC 1)
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

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}/result-0-result.json")
file(REMOVE "${JUNIT_PATH}")

execute_process(
  COMMAND ${_emu} "${PROG}" ${_args} "--allure-dir=${OUT_DIR}" "--junit=${JUNIT_PATH}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

set(_all "${_out}\n${_err}")
if(NOT _rc EQUAL EXPECT_RC)
  message(FATAL_ERROR "Expected exit code ${EXPECT_RC}, got ${_rc}. Output:\n${_all}")
endif()

if(NOT EXISTS "${JUNIT_PATH}")
  message(FATAL_ERROR "Expected JUnit file not found: ${JUNIT_PATH}. Output:\n${_all}")
endif()

file(READ "${JUNIT_PATH}" _junit)
string(FIND "${_junit}" "${EXPECT_JUNIT_COUNTS}" _counts_pos)
if(_counts_pos EQUAL -1)
  message(FATAL_ERROR "Expected JUnit counts substring '${EXPECT_JUNIT_COUNTS}'. File:\n${_junit}")
endif()

string(FIND "${_junit}" "${EXPECT_SUBSTRING}" _msg_pos)
if(_msg_pos EQUAL -1)
  message(FATAL_ERROR "Expected JUnit to include infra failure substring '${EXPECT_SUBSTRING}'. File:\n${_junit}\n\nOutput:\n${_all}")
endif()

string(FIND "${_junit}" "${EXPECT_CASE_NAME}" _case_pos)
if(_case_pos EQUAL -1)
  message(FATAL_ERROR "Expected JUnit to retain testcase '${EXPECT_CASE_NAME}'. File:\n${_junit}\n\nOutput:\n${_all}")
endif()
