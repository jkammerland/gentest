# Requires:
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckPublicModuleGccTsanFallback.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

gentest_find_supported_ninja(_supported_ninja _supported_ninja_reason)
if(NOT _supported_ninja)
  gentest_skip_test("public module GNU+TSAN fallback regression: ${_supported_ninja_reason}")
  return()
endif()

set(CMAKE_GENERATOR "Ninja")
set(CMAKE_MAKE_PROGRAM "${_supported_ninja}")
set(CMAKE_CXX_COMPILER_ID "GNU")
set(CMAKE_CXX_COMPILER_VERSION "15.2.1")
set(CMAKE_CXX_SCANDEP_SOURCE "dummy-scan-source")
set(CMAKE_CXX_FLAGS "-fsanitize=thread -g -O1 -fno-omit-frame-pointer")
set(CMAKE_EXE_LINKER_FLAGS "-fsanitize=thread")
set(CMAKE_MODULE_LINKER_FLAGS "-fsanitize=thread")
set(CMAKE_SHARED_LINKER_FLAGS "-fsanitize=thread")

include("${GENTEST_SOURCE_DIR}/cmake/GentestPublicModules.cmake")

gentest_detect_public_module_support(_supported _reason)
if(_supported)
  message(FATAL_ERROR "Expected public modules to be disabled for GNU 15.2.1 with ThreadSanitizer, but support detection returned enabled")
endif()

set(_expected_reason
  "GNU 15.2.1 with ThreadSanitizer cannot build gentest's public named modules; GNU 16 or newer is required when -fsanitize=thread is enabled")
if(NOT _reason STREQUAL _expected_reason)
  message(FATAL_ERROR "Expected reason '${_expected_reason}', got '${_reason}'")
endif()

set(CMAKE_CXX_COMPILER_VERSION "16.0.0")
gentest_detect_public_module_support(_supported _reason)
if(NOT _supported)
  message(FATAL_ERROR "Expected public modules to stay enabled for GNU 16.0.0 with ThreadSanitizer, but support detection returned disabled: ${_reason}")
endif()

message(STATUS "GNU+TSAN public module fallback regression passed")
