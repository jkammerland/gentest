# Verifies the configuration-specific generated-output contract used by
# gentest_attach_codegen() and late gentest_link_mocks() calls. The fixture
# intentionally attaches codegen before linking an explicit mock target.

foreach(_required IN ITEMS SOURCE_DIR BUILD_ROOT GENTEST_SOURCE_DIR PROG)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "CheckMultiConfigExplicitMocks.cmake: ${_required} not set")
  endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("multi-config explicit mock regression: no usable clang/clang++ pair was provided")
  return()
endif()

gentest_find_supported_ninja(_ninja _ninja_reason)
if(NOT _ninja)
  gentest_skip_test("multi-config explicit mock regression: ${_ninja_reason}")
  return()
endif()

set(_work_dir "${BUILD_ROOT}/multi_config_explicit_mocks")
set(_build_dir "${_work_dir}/build")
gentest_remove_fixture_path("${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_cache_args
  "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}"
  "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}"
  "-DCMAKE_MAKE_PROGRAM=${_ninja}"
  "-DCMAKE_C_COMPILER=${_clang}"
  "-DCMAKE_CXX_COMPILER=${_clangxx}")
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _cache_args "-DClang_DIR=${Clang_DIR}")
endif()
gentest_append_host_apple_sysroot(_cache_args)

gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    -G "Ninja Multi-Config"
    -S "${SOURCE_DIR}"
    -B "${_build_dir}"
    ${_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --config Debug --target explicit_late_link_consumer
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_exe "${_build_dir}/Debug/explicit_late_link_consumer${CMAKE_EXECUTABLE_SUFFIX}")
gentest_check_run_or_fail(
  COMMAND "${_exe}" --no-color
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

file(GLOB _mock_outputs "${_build_dir}/generated/header/Debug/*_mock_registry.hpp")
if(NOT _mock_outputs)
  message(FATAL_ERROR
    "Expected a configuration-specific generated mock registry under '${_build_dir}/generated/header/Debug'.")
endif()

message(STATUS "Ninja Multi-Config explicit mock late-link regression passed")
