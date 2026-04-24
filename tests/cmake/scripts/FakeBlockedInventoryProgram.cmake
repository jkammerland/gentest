set(_mode "")
if(CMAKE_ARGC GREATER 4)
  set(_mode "${CMAKE_ARGV4}")
endif()

set(_blocked_case "fake/blocked")

if(_mode STREQUAL "--list" OR _mode STREQUAL "--list-tests")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "${_blocked_case}")
  return()
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "[ BLOCKED ] ${_blocked_case}")
cmake_language(EXIT 1)
