# Proves a successful empty run replaces a requested JUnit report instead of
# leaving stale evidence from an earlier run in place.
#
# Required: PROG, REPORT_PATH. Optional: EMU, ARGS, EXPECT_RC (default 0).

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckEmptyRunReplacesJunit.cmake: PROG not set")
endif()
if(NOT DEFINED REPORT_PATH)
  message(FATAL_ERROR "CheckEmptyRunReplacesJunit.cmake: REPORT_PATH not set")
endif()
if(NOT DEFINED EXPECT_RC)
  set(EXPECT_RC 0)
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

get_filename_component(_report_dir "${REPORT_PATH}" DIRECTORY)
file(MAKE_DIRECTORY "${_report_dir}")
set(_stale "stale evidence from an earlier run")
file(WRITE "${REPORT_PATH}" "${_stale}")

execute_process(
  COMMAND ${_emu} "${PROG}" ${_args} "--junit=${REPORT_PATH}"
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)

if(NOT rc EQUAL EXPECT_RC)
  message(FATAL_ERROR "Expected exit code ${EXPECT_RC}, got ${rc}\n--- stdout ---\n${out}\n--- stderr ---\n${err}")
endif()

file(READ "${REPORT_PATH}" _report)
string(FIND "${_report}" "${_stale}" _stale_pos)
if(NOT _stale_pos EQUAL -1)
  message(FATAL_ERROR "Requested JUnit report was not replaced; stale content remains in ${REPORT_PATH}")
endif()
if(NOT _report MATCHES "<testsuite[^>]* tests=\"0\"")
  message(FATAL_ERROR "Requested JUnit report is not an empty suite:\n${_report}")
endif()
