include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

if(CMAKE_HOST_WIN32)
  gentest_skip_test("supported ninja portability regression is Unix-host specific")
  return()
endif()

find_program(_fake_make_program NAMES gmake make)
if(NOT _fake_make_program)
  gentest_skip_test("supported ninja portability regression: no non-Ninja make program found")
  return()
endif()

set(GENERATOR "Unix Makefiles")
set(MAKE_PROGRAM "${_fake_make_program}")

gentest_find_supported_ninja(_resolved_ninja _resolved_reason)
if(NOT _resolved_ninja)
  gentest_skip_test("supported ninja portability regression: ${_resolved_reason}")
  return()
endif()

if("${_resolved_ninja}" STREQUAL "${MAKE_PROGRAM}")
  message(FATAL_ERROR
    "Expected gentest_find_supported_ninja() to ignore non-Ninja MAKE_PROGRAM='${MAKE_PROGRAM}', but it returned the same path")
endif()

get_filename_component(_resolved_name "${_resolved_ninja}" NAME)
if(NOT _resolved_name MATCHES "^ninja(|-build)(\\.exe)?$")
  message(FATAL_ERROR "Expected a Ninja executable, got '${_resolved_ninja}'")
endif()
