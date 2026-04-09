# Requires:
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckPublicModuleGccVersionFallback.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

gentest_find_supported_ninja(_supported_ninja _supported_ninja_reason)
if(NOT _supported_ninja)
  gentest_skip_test("public module GNU-version fallback regression: ${_supported_ninja_reason}")
  return()
endif()

set(CMAKE_GENERATOR "Ninja")
set(CMAKE_MAKE_PROGRAM "${_supported_ninja}")
set(CMAKE_CXX_COMPILER_ID "GNU")
set(CMAKE_CXX_COMPILER_VERSION "14.2.1")
set(CMAKE_CXX_SCANDEP_SOURCE "dummy-scan-source")

include("${GENTEST_SOURCE_DIR}/cmake/GentestPublicModules.cmake")

gentest_detect_public_module_support(_supported _reason)
if(_supported)
  message(FATAL_ERROR "Expected public modules to be disabled for GNU 14.2.1, but support detection returned enabled")
endif()

set(_expected_reason "GNU 14.2.1 is too old; GNU 15 or newer is required for public named modules")
if(NOT _reason STREQUAL _expected_reason)
  message(FATAL_ERROR "Expected reason '${_expected_reason}', got '${_reason}'")
endif()

message(STATUS "GNU-version public module fallback regression passed")
