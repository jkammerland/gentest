function(_gentest_resolve_helper_parallel_level out_var)
  set(_helper_parallel_level "${GENTEST_HELPER_BUILD_PARALLEL_LEVEL}")
  if("${_helper_parallel_level}" STREQUAL "" AND NOT "$ENV{GENTEST_HELPER_BUILD_PARALLEL_LEVEL}" STREQUAL "")
    set(_helper_parallel_level "$ENV{GENTEST_HELPER_BUILD_PARALLEL_LEVEL}")
  endif()
  if("${_helper_parallel_level}" STREQUAL "")
    set(_helper_parallel_level "2")
  endif()
  set(${out_var} "${_helper_parallel_level}" PARENT_SCOPE)
endfunction()

function(_gentest_wrap_command_with_helper_parallel_env out_var)
  _gentest_resolve_helper_parallel_level(_helper_parallel_level)
  set(_wrapped
    "${CMAKE_COMMAND}" -E env
      "CMAKE_BUILD_PARALLEL_LEVEL=${_helper_parallel_level}"
      "CTEST_PARALLEL_LEVEL=${_helper_parallel_level}"
      ${ARGN})
  set(${out_var} ${_wrapped} PARENT_SCOPE)
endfunction()

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
    _gentest_wrap_command_with_helper_parallel_env(_command ${RUN_COMMAND})
    execute_process(
      COMMAND ${_command}
      WORKING_DIRECTORY "${RUN_WORKING_DIRECTORY}"
      RESULT_VARIABLE _rc
      OUTPUT_VARIABLE _out
      ERROR_VARIABLE _err
      ${_strip_args}
    )
  else()
    _gentest_wrap_command_with_helper_parallel_env(_command ${RUN_COMMAND})
    execute_process(
      COMMAND ${_command}
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
