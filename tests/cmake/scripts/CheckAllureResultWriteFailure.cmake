if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckAllureResultWriteFailure.cmake: PROG not set")
endif()
if(NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "CheckAllureResultWriteFailure.cmake: OUT_DIR not set")
endif()
if(NOT DEFINED EXPECT_SUBSTRING)
  message(FATAL_ERROR "CheckAllureResultWriteFailure.cmake: EXPECT_SUBSTRING not set")
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

execute_process(
  COMMAND ${_emu} "${PROG}" ${_args} "--allure-dir=${OUT_DIR}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

set(_all "${_out}\n${_err}")
if(NOT _rc EQUAL EXPECT_RC)
  message(FATAL_ERROR "Expected exit code ${EXPECT_RC}, got ${_rc}. Output:\n${_all}")
endif()

set(_infra_result "${OUT_DIR}/result-1-result.json")
if(NOT EXISTS "${_infra_result}")
  message(FATAL_ERROR "Expected infra result not found: ${_infra_result}. Output:\n${_all}")
endif()

file(READ "${_infra_result}" _content)
string(FIND "${_content}" "${EXPECT_SUBSTRING}" _pos)
if(_pos EQUAL -1)
  message(FATAL_ERROR "Expected substring not found in infra result: '${EXPECT_SUBSTRING}'. Content:\n${_content}\n\nOutput:\n${_all}")
endif()

