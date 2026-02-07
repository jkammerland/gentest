# Usage:
#   cmake -DPROG=<path> -DEXPECT_RC=<int> [-DREPEATS=<n>] [-DARGS="--flags ..."] -P cmake/CheckStableExitCode.cmake

if(NOT DEFINED PROG)
  message(FATAL_ERROR "PROG not set")
endif()
if(NOT DEFINED EXPECT_RC)
  message(FATAL_ERROR "EXPECT_RC not set")
endif()
if(NOT DEFINED REPEATS)
  set(REPEATS 1)
endif()
if(REPEATS LESS 1)
  message(FATAL_ERROR "REPEATS must be >= 1")
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

foreach(_iter RANGE 1 ${REPEATS})
  execute_process(
    COMMAND ${_emu} "${PROG}" ${_args}
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc
  )

  if(NOT rc EQUAL EXPECT_RC)
    message(
      FATAL_ERROR
        "Iteration ${_iter}/${REPEATS}: expected exit code ${EXPECT_RC}, got ${rc}. Output:\n${out}\nErrors:\n${err}"
    )
  endif()
endforeach()

message(STATUS "Command returned ${EXPECT_RC} for ${REPEATS} iteration(s)")
