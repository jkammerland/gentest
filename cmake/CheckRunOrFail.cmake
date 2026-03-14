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
    set(_command ${RUN_COMMAND})
    set(_helper_parallel_level "${GENTEST_HELPER_BUILD_PARALLEL_LEVEL}")
    if("${_helper_parallel_level}" STREQUAL "" AND NOT "$ENV{GENTEST_HELPER_BUILD_PARALLEL_LEVEL}" STREQUAL "")
      set(_helper_parallel_level "$ENV{GENTEST_HELPER_BUILD_PARALLEL_LEVEL}")
    endif()
    if("${_helper_parallel_level}" STREQUAL "")
      set(_helper_parallel_level "2")
    endif()
    set(_env_args)
    if("$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
      list(APPEND _env_args "CMAKE_BUILD_PARALLEL_LEVEL=${_helper_parallel_level}")
    endif()
    if("$ENV{CTEST_PARALLEL_LEVEL}" STREQUAL "")
      list(APPEND _env_args "CTEST_PARALLEL_LEVEL=${_helper_parallel_level}")
    endif()
    if(_env_args)
      set(_command "${CMAKE_COMMAND}" -E env ${_env_args} ${RUN_COMMAND})
    endif()
    execute_process(
      COMMAND ${_command}
      WORKING_DIRECTORY "${RUN_WORKING_DIRECTORY}"
      RESULT_VARIABLE _rc
      OUTPUT_VARIABLE _out
      ERROR_VARIABLE _err
      ${_strip_args}
    )
  else()
    set(_command ${RUN_COMMAND})
    set(_helper_parallel_level "${GENTEST_HELPER_BUILD_PARALLEL_LEVEL}")
    if("${_helper_parallel_level}" STREQUAL "" AND NOT "$ENV{GENTEST_HELPER_BUILD_PARALLEL_LEVEL}" STREQUAL "")
      set(_helper_parallel_level "$ENV{GENTEST_HELPER_BUILD_PARALLEL_LEVEL}")
    endif()
    if("${_helper_parallel_level}" STREQUAL "")
      set(_helper_parallel_level "2")
    endif()
    set(_env_args)
    if("$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
      list(APPEND _env_args "CMAKE_BUILD_PARALLEL_LEVEL=${_helper_parallel_level}")
    endif()
    if("$ENV{CTEST_PARALLEL_LEVEL}" STREQUAL "")
      list(APPEND _env_args "CTEST_PARALLEL_LEVEL=${_helper_parallel_level}")
    endif()
    if(_env_args)
      set(_command "${CMAKE_COMMAND}" -E env ${_env_args} ${RUN_COMMAND})
    endif()
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
