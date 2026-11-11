# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DGENERATOR=<cmake generator name>
# Optional:
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain.cmake>
#  -DMAKE_PROGRAM=<path>
#  -DC_COMPILER=<path>
#  -DCXX_COMPILER=<path>
#  -DBUILD_TYPE=<Debug|Release|...>
#  -DLLVM_DIR=<llvm cmake dir>
#  -DClang_DIR=<clang cmake dir>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckFmtVersionMetadataGuard.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckFmtVersionMetadataGuard.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckFmtVersionMetadataGuard.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/fmt_version_metadata_guard")
set(_build_dir_ok "${_work_dir}/build-ok")
set(_build_dir_install_fail "${_work_dir}/build-install-fail")
set(_build_dir_install_ok "${_work_dir}/build-install-ok")
set(_install_prefix_ok "${_work_dir}/install-prefix")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_common_cache_args
  "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}")
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _common_cache_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _common_cache_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _common_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _common_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _common_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

gentest_append_llvm_package_cache_args(_common_cache_args)

message(STATUS "Configure fmt metadata guard fixture with gentest_INSTALL=OFF...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_build_dir_ok}"
    ${_common_cache_args}
    "-Dgentest_INSTALL=OFF"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Configure fmt metadata guard fixture with gentest_INSTALL=ON (expected failure)...")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_build_dir_install_fail}"
    ${_common_cache_args}
    "-Dgentest_INSTALL=ON"
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(_rc EQUAL 0)
  message(FATAL_ERROR
    "fmt metadata guard fixture unexpectedly configured with gentest_INSTALL=ON even though the predefined fmt::fmt target has no version metadata")
endif()

set(_all "${_out}\n${_err}")
string(FIND "${_all}" "failed to determine resolved fmt version" _msg_pos)
if(_msg_pos EQUAL -1)
  message(FATAL_ERROR
    "Unexpected failure while probing fmt version metadata guard.\n"
    "stdout:\n${_out}\n"
    "stderr:\n${_err}")
endif()

message(STATUS "fmt metadata guard passed")

message(STATUS "Configure fmt metadata guard fixture with gentest_INSTALL=ON and explicit fmt_VERSION...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_build_dir_install_ok}"
    ${_common_cache_args}
    "-Dgentest_INSTALL=ON"
    "-Dfmt_VERSION=12.1.0"
    "-DCMAKE_INSTALL_PREFIX=${_install_prefix_ok}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

# gentestConfig.cmake is generated at configure time and installed verbatim
# (install(FILES)), so the build-tree file is what consumers get. Assert on it
# and on the install rule that ships it instead of building the runtime and
# codegen just to run the install step.
file(GLOB_RECURSE _config_candidates
  LIST_DIRECTORIES FALSE
  "${_build_dir_install_ok}/gentestConfig.cmake")
list(FILTER _config_candidates EXCLUDE REGEX "/CMakeFiles/")
list(LENGTH _config_candidates _config_candidate_count)
if(NOT _config_candidate_count EQUAL 1)
  message(FATAL_ERROR
    "fmt metadata guard expected exactly one generated gentestConfig.cmake under '${_build_dir_install_ok}', "
    "found: ${_config_candidates}")
endif()
list(GET _config_candidates 0 _config_file)

file(GLOB_RECURSE _install_scripts LIST_DIRECTORIES FALSE "${_build_dir_install_ok}/cmake_install.cmake")
set(_config_install_rule_found FALSE)
foreach(_install_script IN LISTS _install_scripts)
  file(READ "${_install_script}" _install_script_text)
  string(FIND "${_install_script_text}" "\"${_config_file}\"" _config_install_pos)
  if(NOT _config_install_pos EQUAL -1)
    set(_config_install_rule_found TRUE)
    break()
  endif()
endforeach()
if(NOT _config_install_rule_found)
  message(FATAL_ERROR
    "fmt metadata guard: no install rule ships the generated config '${_config_file}'")
endif()

file(READ "${_config_file}" _config_text)
string(FIND "${_config_text}" "find_dependency(fmt 12.1.0 EXACT CONFIG REQUIRED)" _config_dep_pos)
if(_config_dep_pos EQUAL -1)
  message(FATAL_ERROR
    "fmt metadata guard installed config must require the explicit fmt_VERSION value.\n"
    "Config file: ${_config_file}")
endif()

message(STATUS "fmt metadata guard passed")
