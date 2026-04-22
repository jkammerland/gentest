# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckPublicModuleSurface.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckPublicModuleSurface.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckPublicModuleSurface.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/public_module_surface")
set(_src_dir "${_work_dir}/src")
set(_producer_build_dir "${_work_dir}/producer")
set(_consumer_build_dir "${_work_dir}/consumer")
set(_install_prefix "${_work_dir}/install")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("public module surface regression: clang/clang++ not found")
  return()
endif()

gentest_find_supported_ninja(_ninja _ninja_reason)
if(NOT _ninja)
  gentest_skip_test("public module surface regression: ${_ninja_reason}")
  return()
endif()

gentest_find_clang_scan_deps(_scan_deps "${_clangxx}")
if(NOT _scan_deps)
  gentest_skip_test("public module surface regression: clang-scan-deps not found for '${_clangxx}'")
  return()
endif()

set(_common_cache_args
  "-DCMAKE_MAKE_PROGRAM=${_ninja}"
  "-DCMAKE_C_COMPILER=${_clang}"
  "-DCMAKE_CXX_COMPILER=${_clangxx}"
  "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_scan_deps}")
gentest_resolve_fixture_build_type(_effective_build_type "${_clangxx}" "${BUILD_TYPE}")
if(NOT "${_effective_build_type}" STREQUAL "")
  list(APPEND _common_cache_args "-DCMAKE_BUILD_TYPE=${_effective_build_type}")
endif()
gentest_append_windows_native_llvm_cache_args(_common_cache_args "${_clangxx}" ${_common_cache_args})
gentest_append_host_apple_sysroot(_common_cache_args)

message(STATUS "Configure producer for public module surface regression...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    -G Ninja
    -S "${GENTEST_SOURCE_DIR}"
    -B "${_producer_build_dir}"
    ${_common_cache_args}
    "-Dgentest_INSTALL=ON"
    "-Dgentest_BUILD_TESTING=OFF"
    "-DGENTEST_BUILD_CODEGEN=ON"
    "-DGENTEST_ENABLE_PUBLIC_MODULES=ON"
    "-DCMAKE_INSTALL_PREFIX=${_install_prefix}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
gentest_assert_windows_native_llvm_cache_args(
  "${_producer_build_dir}" "${_clangxx}" "public module surface producer")

message(STATUS "Build and install producer for public module surface regression...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_producer_build_dir}" --target install
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Configure public module consumer fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    -G Ninja
    -S "${_src_dir}"
    -B "${_consumer_build_dir}"
    ${_common_cache_args}
    "-DCMAKE_PREFIX_PATH=${_install_prefix}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
gentest_assert_windows_native_llvm_cache_args(
  "${_consumer_build_dir}" "${_clangxx}" "public module surface consumer")

message(STATUS "Build public module consumer fixture...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_consumer_build_dir}" --target public_module_surface
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_exe "${_consumer_build_dir}/public_module_surface${CMAKE_EXECUTABLE_SUFFIX}")
gentest_check_run_or_fail(
  COMMAND "${_exe}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

function(gentest_expect_generated_boundary file required_include)
  if(NOT EXISTS "${file}")
    message(FATAL_ERROR "Expected generated public-module artifact '${file}' to exist")
  endif()
  file(READ "${file}" _generated_text)
  string(FIND "${_generated_text}" "${required_include}" _required_pos)
  if(_required_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected generated public-module artifact '${file}' to contain '${required_include}'.\n${_generated_text}")
  endif()
  foreach(_rejected IN ITEMS
      "#include \"gentest/detail/fixture_runtime.h\""
      "#include \"gentest/detail/registry_runtime.h\"")
    string(FIND "${_generated_text}" "${_rejected}" _rejected_pos)
    if(NOT _rejected_pos EQUAL -1)
      message(FATAL_ERROR
        "Generated public-module artifact '${file}' should not include '${_rejected}'.\n${_generated_text}")
    endif()
  endforeach()
endfunction()

gentest_expect_generated_boundary(
  "${_consumer_build_dir}/gentest_codegen/tu_0000_cases.registration.gentest.cpp"
  "#include \"gentest/detail/generated_runtime.h\"")
gentest_expect_generated_boundary(
  "${_consumer_build_dir}/gentest_codegen/tu_0000_cases.gentest.h"
  "#include \"gentest/detail/registration_runtime.h\"")

function(gentest_expect_public_module_hidden_target target expected_api_pattern)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${_consumer_build_dir}" --target "${target}"
    WORKING_DIRECTORY "${_work_dir}"
    RESULT_VARIABLE _hidden_rc
    OUTPUT_VARIABLE _hidden_out
    ERROR_VARIABLE _hidden_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if(_hidden_rc EQUAL 0)
    message(FATAL_ERROR
      "Public module target '${target}' should not compile; generated-runtime detail APIs must stay hidden.\n"
      "--- stdout ---\n${_hidden_out}\n--- stderr ---\n${_hidden_err}")
  endif()

  set(_hidden_all_output "${_hidden_out}\n${_hidden_err}")
  if(NOT _hidden_all_output MATCHES "gentest::detail"
     OR NOT _hidden_all_output MATCHES "${expected_api_pattern}")
    message(FATAL_ERROR
      "Public module target '${target}' failed, but not with the expected hidden API diagnostic.\n"
      "Expected output to mention gentest::detail and '${expected_api_pattern}'.\n"
      "--- stdout ---\n${_hidden_out}\n--- stderr ---\n${_hidden_err}")
  endif()
endfunction()

gentest_expect_public_module_hidden_target(public_module_hides_register_cases "register_cases")
gentest_expect_public_module_hidden_target(public_module_hides_snapshot_registered_cases "snapshot_registered_cases")
gentest_expect_public_module_hidden_target(public_module_hides_shared_fixture_scope "SharedFixtureScope")
gentest_expect_public_module_hidden_target(public_module_hides_register_shared_fixture "register_shared_fixture|SharedFixtureScope")

message(STATUS "Observed public module surface consumer success")
