# Requires:
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DBUILD_ROOT=<path to parent build dir>

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleNameLiteralFalseMatch.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleNameLiteralFalseMatch.cmake: BUILD_ROOT not set")
endif()

set(_work_dir "${BUILD_ROOT}/module_name_literal_false_match")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_source "${_work_dir}/literal_false_match.cppm")
file(WRITE "${_source}"
  "#define BANNER \"export module wrong.module;\"\n"
  "export module real.module;\n")

include("${GENTEST_SOURCE_DIR}/cmake/GentestCodegen.cmake")

_gentest_extract_module_name("${_source}" _module_name)

if(NOT _module_name STREQUAL "real.module")
  message(FATAL_ERROR
    "Expected module name 'real.module', got '${_module_name}'")
endif()

message(STATUS "Module-name literal false-match regression passed")
