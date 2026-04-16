# Requires:
#  -DPROG=<path to gentest_codegen>
#  -DBUILD_ROOT=<top-level build tree>
#  -DGENTEST_SOURCE_DIR=<gentest source tree>
#  -DSOURCE_DIR=<fixture source dir>
#  -DTARGET_NAME=<fixture target to build>
#  -DGENERATOR=<cmake generator>
# Optional:
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain>
#  -DMAKE_PROGRAM=<make/ninja path>
#  -DC_COMPILER=<C compiler>
#  -DCXX_COMPILER=<C++ compiler>
#  -DCXX_COMPILER_CLANG_SCAN_DEPS=<clang-scan-deps binary>
#  -DBUILD_TYPE=<Debug/Release/...>
#  -DBUILD_CONFIG=<Debug/Release/...>  # for multi-config generators
#  -DBUILD_TIMEOUT_SEC=<seconds>
#  -DLLVM_DIR=<llvm cmake dir>
#  -DClang_DIR=<clang cmake dir>

if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCmakeFixtureBuild.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCmakeFixtureBuild.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCmakeFixtureBuild.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCmakeFixtureBuild.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED TARGET_NAME OR "${TARGET_NAME}" STREQUAL "")
  message(FATAL_ERROR "CheckCmakeFixtureBuild.cmake: TARGET_NAME not set")
endif()
if(NOT DEFINED GENERATOR OR "${GENERATOR}" STREQUAL "")
  message(FATAL_ERROR "CheckCmakeFixtureBuild.cmake: GENERATOR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

# Keep nested fixture scratch roots compact so long target names do not get
# repeated beneath an already-short helper BUILD_ROOT on Windows.
string(MD5 _fixture_work_hash "${TARGET_NAME}|${SOURCE_DIR}")
string(SUBSTRING "${_fixture_work_hash}" 0 12 _fixture_work_suffix)
set(_work_dir "${BUILD_ROOT}/cfb_${_fixture_work_suffix}")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_build_dir "${_work_dir}/build")

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}"
  "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}")
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED CXX_COMPILER_CLANG_SCAN_DEPS AND NOT "${CXX_COMPILER_CLANG_SCAN_DEPS}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${CXX_COMPILER_CLANG_SCAN_DEPS}")
endif()
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DClang_DIR=${Clang_DIR}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

set(_fixture_uses_public_modules FALSE)
file(GLOB_RECURSE _fixture_source_candidates
  LIST_DIRECTORIES FALSE
  "${SOURCE_DIR}/*.cpp"
  "${SOURCE_DIR}/*.cc"
  "${SOURCE_DIR}/*.cxx"
  "${SOURCE_DIR}/*.cppm"
  "${SOURCE_DIR}/*.ixx"
  "${SOURCE_DIR}/*.mpp")
foreach(_fixture_source IN LISTS _fixture_source_candidates)
  file(READ "${_fixture_source}" _fixture_source_text)
  if(_fixture_source_text MATCHES "(^|[\r\n])[ \t]*(export[ \t]+)?import[ \t]+gentest([.;]|\\.)")
    set(_fixture_uses_public_modules TRUE)
    break()
  endif()
endforeach()
if(_fixture_uses_public_modules)
  list(APPEND _cmake_cache_args "-DGENTEST_ENABLE_PUBLIC_MODULES=AUTO")
elseif(CMAKE_HOST_WIN32)
  # Textual/mock helper fixtures do not need gentest's public modules on
  # Windows, and disabling them keeps nested object paths shorter.
  list(APPEND _cmake_cache_args "-DGENTEST_ENABLE_PUBLIC_MODULES=OFF")
endif()
if(CMAKE_HOST_WIN32)
  list(APPEND _cmake_cache_args "-DCMAKE_OBJECT_PATH_MAX=128")
endif()

if(_fixture_uses_public_modules)
  set(_module_probe_source_dir "${_work_dir}/module_probe_src")
  set(_module_probe_build_dir "${_work_dir}/module_probe_build")
  file(MAKE_DIRECTORY "${_module_probe_source_dir}")
  file(TO_CMAKE_PATH "${GENTEST_SOURCE_DIR}" _module_probe_gentest_source_dir)
  file(WRITE "${_module_probe_source_dir}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.31)\n"
    "project(gentest_public_module_probe LANGUAGES C CXX)\n"
    "set(gentest_BUILD_TESTING OFF CACHE BOOL \"\" FORCE)\n"
    "set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL \"\" FORCE)\n"
    "add_subdirectory(\"${_module_probe_gentest_source_dir}\" gentest)\n")

  message(STATUS "Probe gentest public module support for ${TARGET_NAME} fixture...")
  gentest_check_run_or_fail(
    COMMAND
      "${CMAKE_COMMAND}"
      ${_cmake_gen_args}
      -S "${_module_probe_source_dir}"
      -B "${_module_probe_build_dir}"
      ${_cmake_cache_args}
    STRIP_TRAILING_WHITESPACE
    WORKING_DIRECTORY "${_work_dir}")

  set(_module_probe_cache_file "${_module_probe_build_dir}/CMakeCache.txt")
  gentest_read_cache_value("${_module_probe_cache_file}" "GENTEST_PUBLIC_MODULES_ENABLED"
    _modules_enabled_found _modules_enabled_value)
  if(_modules_enabled_found AND NOT _modules_enabled_value)
    gentest_read_cache_value("${_module_probe_cache_file}" "GENTEST_PUBLIC_MODULES_DISABLED_REASON"
      _modules_reason_found _modules_disabled_reason)
    if(NOT _modules_reason_found OR "${_modules_disabled_reason}" STREQUAL "")
      set(_modules_disabled_reason "module support was disabled during fixture configure")
    endif()
    gentest_skip_test("${TARGET_NAME} fixture requires gentest public named modules: ${_modules_disabled_reason}")
    return()
  endif()
endif()

message(STATUS "Configure ${TARGET_NAME} fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}")

if(_fixture_uses_public_modules)
  set(_fixture_cache_file "${_build_dir}/CMakeCache.txt")
  gentest_read_cache_value("${_fixture_cache_file}" "GENTEST_PUBLIC_MODULES_ENABLED"
    _modules_enabled_found _modules_enabled_value)
  if(_modules_enabled_found AND NOT _modules_enabled_value)
    gentest_read_cache_value("${_fixture_cache_file}" "GENTEST_PUBLIC_MODULES_DISABLED_REASON"
      _modules_reason_found _modules_disabled_reason)
    if(NOT _modules_reason_found OR "${_modules_disabled_reason}" STREQUAL "")
      set(_modules_disabled_reason "module support was disabled during fixture configure")
    endif()
    gentest_skip_test("${TARGET_NAME} fixture requires gentest public named modules: ${_modules_disabled_reason}")
    return()
  endif()
endif()

set(_build_cmd
  "${CMAKE_COMMAND}"
  --build "${_build_dir}"
  --target "${TARGET_NAME}")
if(DEFINED BUILD_CONFIG AND NOT "${BUILD_CONFIG}" STREQUAL "")
  list(APPEND _build_cmd --config "${BUILD_CONFIG}")
elseif(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _build_cmd --config "${BUILD_TYPE}")
endif()

if(DEFINED BUILD_TIMEOUT_SEC AND NOT "${BUILD_TIMEOUT_SEC}" STREQUAL "")
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

  set(_command ${_build_cmd})
  if(_env_args)
    set(_command "${CMAKE_COMMAND}" -E env ${_env_args} ${_build_cmd})
  endif()

  message(STATUS "Build ${TARGET_NAME} fixture with timeout ${BUILD_TIMEOUT_SEC}s...")
  execute_process(
    COMMAND ${_command}
    WORKING_DIRECTORY "${_work_dir}"
    TIMEOUT "${BUILD_TIMEOUT_SEC}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if(NOT _rc MATCHES "^-?[0-9]+$")
    if(_rc STREQUAL "Process terminated due to timeout")
      message(FATAL_ERROR "Build timed out for ${TARGET_NAME} after ${BUILD_TIMEOUT_SEC}s.\n--- stdout ---\n${_out}\n--- stderr ---\n${_err}\n")
    endif()
    message(FATAL_ERROR "Build failed (${_rc}) for ${TARGET_NAME}.\n--- stdout ---\n${_out}\n--- stderr ---\n${_err}\n")
  endif()
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Build failed (${_rc}) for ${TARGET_NAME}.\n--- stdout ---\n${_out}\n--- stderr ---\n${_err}\n")
  endif()
else()
  message(STATUS "Build ${TARGET_NAME} fixture...")
  gentest_check_run_or_fail(
    COMMAND
      ${_build_cmd}
    STRIP_TRAILING_WHITESPACE
    WORKING_DIRECTORY "${_work_dir}")
endif()

message(STATUS "${TARGET_NAME} fixture built successfully")
