# CTest fixture for the shared public-module producer.
# Requires:
#  -DBUILD_ROOT=<fixture root; the producer lives in <BUILD_ROOT>/p and <BUILD_ROOT>/i>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DPUBLIC_MODULE_PRODUCER_PHASE=<SETUP|CLEANUP>
# Optional:
#  -DLLVM_DIR=<llvm cmake dir>
#  -DClang_DIR=<clang cmake dir>
#  -DBUILD_TYPE=<Debug|Release|...>

if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "PublicModuleProducerFixture.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "PublicModuleProducerFixture.cmake: GENTEST_SOURCE_DIR not set")
endif()

set(PUBLIC_MODULE_PRODUCER_ROOT "${BUILD_ROOT}")
include("${CMAKE_CURRENT_LIST_DIR}/PublicModuleProducer.cmake")
gentest_public_module_toolchain()

if(PUBLIC_MODULE_PRODUCER_PHASE STREQUAL "CLEANUP")
  if(IS_DIRECTORY "${BUILD_ROOT}")
    gentest_remove_fixture_path("${_pm_producer_build_dir}")
    gentest_remove_fixture_path("${_pm_install_prefix}")
  endif()
  return()
elseif(NOT PUBLIC_MODULE_PRODUCER_PHASE STREQUAL "SETUP")
  message(FATAL_ERROR "Unknown PUBLIC_MODULE_PRODUCER_PHASE: '${PUBLIC_MODULE_PRODUCER_PHASE}'")
endif()

file(MAKE_DIRECTORY "${BUILD_ROOT}")
gentest_remove_fixture_path("${_pm_producer_build_dir}")
gentest_remove_fixture_path("${_pm_install_prefix}")

if(NOT _pm_skip_reason STREQUAL "")
  # Not a skip marker: dependent tests resolve the same toolchain and skip
  # with this reason themselves.
  message(STATUS "Shared public-module producer not built: ${_pm_skip_reason}")
  return()
endif()

set(_producer_cache_args
  "-DCMAKE_MAKE_PROGRAM=${_ninja}"
  "-DCMAKE_C_COMPILER=${_clang}"
  "-DCMAKE_CXX_COMPILER=${_clangxx}"
  "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_target_scan_deps}")
if(CMAKE_HOST_WIN32)
  list(APPEND _producer_cache_args "-DCMAKE_OBJECT_PATH_MAX=160")
endif()
gentest_resolve_fixture_build_type(_effective_build_type "${_clangxx}" "${BUILD_TYPE}")
if(NOT "${_effective_build_type}" STREQUAL "")
  list(APPEND _producer_cache_args "-DCMAKE_BUILD_TYPE=${_effective_build_type}")
endif()
gentest_append_windows_native_llvm_cache_args(_producer_cache_args "${_clangxx}" ${_producer_cache_args})
gentest_append_host_apple_sysroot(_producer_cache_args)
gentest_append_llvm_package_cache_args(_producer_cache_args)

message(STATUS "Configure shared public-module producer...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    -G Ninja
    -S "${GENTEST_SOURCE_DIR}"
    -B "${_pm_producer_build_dir}"
    ${_producer_cache_args}
    "-Dgentest_INSTALL=ON"
    "-Dgentest_BUILD_TESTING=OFF"
    "-DGENTEST_BUILD_CODEGEN=ON"
    "-DGENTEST_ENABLE_PUBLIC_MODULES=ON"
    "-DCMAKE_INSTALL_PREFIX=${_pm_install_prefix}"
  WORKING_DIRECTORY "${BUILD_ROOT}"
  STRIP_TRAILING_WHITESPACE)
gentest_assert_windows_native_llvm_cache_args(
  "${_pm_producer_build_dir}" "${_clangxx}" "shared public-module producer")

message(STATUS "Build and install shared public-module producer...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_pm_producer_build_dir}" --target install
  WORKING_DIRECTORY "${BUILD_ROOT}"
  STRIP_TRAILING_WHITESPACE)
