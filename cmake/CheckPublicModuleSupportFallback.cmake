# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DFALLBACK_MODE=<old_ninja|missing_scan_deps>
# Optional:
#  -DMAKE_PROGRAM=<path to the real ninja>
#  -DTOOLCHAIN_FILE=<path>
#  -DBUILD_TYPE=<config>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckPublicModuleSupportFallback.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckPublicModuleSupportFallback.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckPublicModuleSupportFallback.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED FALLBACK_MODE OR "${FALLBACK_MODE}" STREQUAL "")
  message(FATAL_ERROR "CheckPublicModuleSupportFallback.cmake: FALLBACK_MODE not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/public_module_support_fallback_${FALLBACK_MODE}")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  message(STATUS "Skipping public module fallback regression (${FALLBACK_MODE}): clang/clang++ not found")
  return()
endif()

gentest_find_supported_ninja(_real_ninja _real_ninja_reason)
if(NOT _real_ninja)
  message(STATUS "Skipping public module fallback regression (${FALLBACK_MODE}): ${_real_ninja_reason}")
  return()
endif()

set(_cmake_cache_args
  "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}"
  "-DCMAKE_C_COMPILER=${_clang}"
  "-DCMAKE_CXX_COMPILER=${_clangxx}")
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

if(FALLBACK_MODE STREQUAL "old_ninja")
  if(CMAKE_HOST_WIN32)
    set(_fake_make_program "${_work_dir}/fake-ninja.cmd")
    file(WRITE "${_fake_make_program}"
      "@echo off\r\n"
      "if \"%1\"==\"--version\" (\r\n"
      "  echo 1.10.0\r\n"
      "  exit /b 0\r\n"
      ")\r\n"
      "\"${_real_ninja}\" %*\r\n")
  else()
    set(_fake_make_program "${_work_dir}/fake-ninja.sh")
    file(WRITE "${_fake_make_program}"
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--version\" ]; then\n"
      "  echo 1.10.0\n"
      "  exit 0\n"
      "fi\n"
      "exec \"${_real_ninja}\" \"$@\"\n")
    file(CHMOD "${_fake_make_program}"
      PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  endif()
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${_fake_make_program}")
  set(_expect_substring "Ninja 1.10.0 is too old")
elseif(FALLBACK_MODE STREQUAL "missing_scan_deps")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${_real_ninja}")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_work_dir}/does-not-exist-clang-scan-deps")
  set(_expect_substring "clang dependency scanner was not found")
else()
  message(FATAL_ERROR "Unknown FALLBACK_MODE='${FALLBACK_MODE}'")
endif()

message(STATUS "Configure public module fallback fixture (${FALLBACK_MODE})...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    -G Ninja
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE _configure_out)

string(FIND "${_configure_out}" "${_expect_substring}" _reason_pos)
if(_reason_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected configure output to mention '${_expect_substring}', but it did not.\n${_configure_out}")
endif()

string(FIND "${_configure_out}" "public named modules disabled automatically" _disabled_pos)
if(_disabled_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected configure output to mention 'public named modules disabled automatically', but it did not.\n${_configure_out}")
endif()

message(STATUS "Build public module fallback fixture (${FALLBACK_MODE})...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target fallback_consumer
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Public module fallback regression passed (${FALLBACK_MODE})")
