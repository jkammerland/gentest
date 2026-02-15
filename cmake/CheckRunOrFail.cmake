function(gentest_check_run_or_fail)
  set(options STRIP_TRAILING_WHITESPACE)
  set(oneValueArgs DISPLAY_COMMAND OUTPUT_VARIABLE WORKING_DIRECTORY)
  set(multiValueArgs COMMAND)
  cmake_parse_arguments(RUN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT RUN_COMMAND)
    message(FATAL_ERROR "gentest_check_run_or_fail: COMMAND is required")
  endif()

  set(_strip_args)
  if(RUN_STRIP_TRAILING_WHITESPACE)
    set(_strip_args OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
  endif()
  if(DEFINED RUN_WORKING_DIRECTORY AND NOT RUN_WORKING_DIRECTORY STREQUAL "")
    execute_process(
      COMMAND ${RUN_COMMAND}
      WORKING_DIRECTORY "${RUN_WORKING_DIRECTORY}"
      RESULT_VARIABLE _rc
      OUTPUT_VARIABLE _out
      ERROR_VARIABLE _err
      ${_strip_args}
    )
  else()
    execute_process(
      COMMAND ${RUN_COMMAND}
      RESULT_VARIABLE _rc
      OUTPUT_VARIABLE _out
      ERROR_VARIABLE _err
      ${_strip_args}
    )
  endif()

  set(_display_command "${RUN_COMMAND}")
  if(DEFINED RUN_DISPLAY_COMMAND AND NOT RUN_DISPLAY_COMMAND STREQUAL "")
    set(_display_command "${RUN_DISPLAY_COMMAND}")
  endif()
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Command failed (${_rc}): ${_display_command}\n--- stdout ---\n${_out}\n--- stderr ---\n${_err}\n")
  endif()

  if(DEFINED RUN_OUTPUT_VARIABLE AND NOT RUN_OUTPUT_VARIABLE STREQUAL "")
    set(${RUN_OUTPUT_VARIABLE} "${_out}\n${_err}" PARENT_SCOPE)
  endif()
endfunction()
