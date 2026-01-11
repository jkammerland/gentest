# Usage:
#   cmake -DROOT=<dir> -DSUBSTRING=<text> -P cmake/CheckNoFuzztestLeak.cmake

if(NOT DEFINED ROOT)
  message(FATAL_ERROR "ROOT not set")
endif()
if(NOT DEFINED SUBSTRING)
  message(FATAL_ERROR "SUBSTRING not set")
endif()

file(GLOB_RECURSE _files "${ROOT}/*")
foreach(_f IN LISTS _files)
  if(IS_DIRECTORY "${_f}")
    continue()
  endif()
  file(READ "${_f}" _content)
  string(FIND "${_content}" "${SUBSTRING}" _pos)
  if(NOT _pos EQUAL -1)
    message(FATAL_ERROR "Found forbidden substring '${SUBSTRING}' in file: ${_f}")
  endif()
endforeach()

