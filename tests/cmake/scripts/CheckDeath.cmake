# Requires:
#  -DPROG=<path to test binary>
#  -DARGS=<optional CLI args>
#  -DENV_VARS=<optional env vars (list of KEY=VALUE)>
#  -DREQUIRED_SUBSTRING=<substring expected in combined output>
#  -DREQUIRED_SUBSTRINGS=<list of substrings expected in combined output>

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckDeath.cmake: PROG not set")
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
    set(_args ${ARGS})
  else()
    separate_arguments(_args NATIVE_COMMAND "${ARGS}")
  endif()
endif()

set(_command ${_emu} "${PROG}" ${_args})
if(DEFINED ENV_VARS)
  set(_env)
  foreach(kv IN LISTS ENV_VARS)
    list(APPEND _env "${kv}")
  endforeach()
  set(_command ${CMAKE_COMMAND} -E env ${_env} ${_emu} "${PROG}" ${_args})
endif()

execute_process(
  COMMAND ${_command}
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

set(_all "${_out}\n${_err}")

if(_rc EQUAL 0)
  message(FATAL_ERROR "Expected process to abort/exit non-zero, but exit code was 0. Output:\n${_all}")
endif()

if(DEFINED REQUIRED_SUBSTRING)
  string(FIND "${_all}" "${REQUIRED_SUBSTRING}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "Expected substring not found in output: '${REQUIRED_SUBSTRING}'. Output:\n${_all}")
  endif()
endif()

if(DEFINED REQUIRED_SUBSTRINGS)
  foreach(_required_substring IN LISTS REQUIRED_SUBSTRINGS)
    string(FIND "${_all}" "${_required_substring}" _pos)
    if(_pos EQUAL -1)
      message(FATAL_ERROR "Expected substring not found in output: '${_required_substring}'. Output:\n${_all}")
    endif()
  endforeach()
endif()

if(DEFINED REQUIRED_SUBSTRINGS_COUNT)
  if(REQUIRED_SUBSTRINGS_COUNT LESS 1)
    message(FATAL_ERROR "REQUIRED_SUBSTRINGS_COUNT must be positive when defined")
  endif()
  math(EXPR _required_substrings_last "${REQUIRED_SUBSTRINGS_COUNT} - 1")
  foreach(_required_substring_index RANGE 0 ${_required_substrings_last})
    set(_required_substring_var "REQUIRED_SUBSTRING_${_required_substring_index}")
    if(NOT DEFINED ${_required_substring_var})
      message(FATAL_ERROR "Missing encoded required substring ${_required_substring_var}")
    endif()
    set(_required_substring "${${_required_substring_var}}")
    string(FIND "${_all}" "${_required_substring}" _pos)
    if(_pos EQUAL -1)
      message(FATAL_ERROR "Expected substring not found in output: '${_required_substring}'. Output:\n${_all}")
    endif()
  endforeach()
endif()

message(STATUS "Death test passed (non-zero exit and expected output present)")
