if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckAllureDirFailure.cmake: PROG not set")
endif()
if(NOT DEFINED OUT_PATH)
  message(FATAL_ERROR "CheckAllureDirFailure.cmake: OUT_PATH not set")
endif()
if(NOT DEFINED REQUIRED_SUBSTRING)
  message(FATAL_ERROR "CheckAllureDirFailure.cmake: REQUIRED_SUBSTRING not set")
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

get_filename_component(_out_parent "${OUT_PATH}" DIRECTORY)
file(REMOVE_RECURSE "${OUT_PATH}")
file(MAKE_DIRECTORY "${_out_parent}")
file(WRITE "${OUT_PATH}" "not-a-directory\n")

execute_process(
  COMMAND ${_emu} "${PROG}" ${_args} "--allure-dir=${OUT_PATH}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

set(_all "${_out}\n${_err}")
if(NOT _rc EQUAL EXPECT_RC)
  message(FATAL_ERROR "Expected exit code ${EXPECT_RC}, got ${_rc}. Output:\n${_all}")
endif()

string(FIND "${_all}" "${REQUIRED_SUBSTRING}" _pos)
if(_pos EQUAL -1)
  message(FATAL_ERROR "Expected substring not found in output: '${REQUIRED_SUBSTRING}'. Output:\n${_all}")
endif()

