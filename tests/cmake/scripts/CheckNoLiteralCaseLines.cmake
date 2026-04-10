# Usage:
#   cmake -DFILES="file1|file2|..." -P tests/cmake/scripts/CheckNoLiteralCaseLines.cmake
#
# Fails when any listed file contains a hard-coded numeric Case.line initializer
# (".line = 123"). This guards against stale manual line mappings.

if(NOT DEFINED FILES OR "${FILES}" STREQUAL "")
  message(FATAL_ERROR "FILES not set")
endif()

string(REPLACE "|" ";" _files "${FILES}")
set(_offenders "")

foreach(_file IN LISTS _files)
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "Input file does not exist: ${_file}")
  endif()
  file(READ "${_file}" _content)
  string(REGEX MATCHALL "\\.line[ \t]*=[ \t]*[0-9]+" _matches "${_content}")
  if(_matches)
    list(APPEND _offenders "${_file}")
  endif()
endforeach()

if(_offenders)
  string(JOIN "\n  " _joined ${_offenders})
  message(FATAL_ERROR "Found hard-coded numeric Case.line initializers in:\n  ${_joined}\nUse __LINE__-based initializers instead.")
endif()
