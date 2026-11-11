# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DPUBLIC_MODULE_PRODUCER_ROOT=<shared producer from PublicModuleProducerFixture.cmake>
# Optional:
#  -DLLVM_DIR=<llvm cmake dir>
#  -DClang_DIR=<clang cmake dir>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckPublicModuleSurface.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckPublicModuleSurface.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckPublicModuleSurface.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/PublicModuleProducer.cmake")

set(_work_dir "${BUILD_ROOT}/public_module_surface")
set(_src_dir "${_work_dir}/src")
set(_consumer_build_dir "${_work_dir}/consumer")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_public_module_toolchain()
if(NOT _pm_skip_reason STREQUAL "")
  gentest_skip_test("public module surface regression: ${_pm_skip_reason}")
  return()
endif()
# The fixture setup installs the modules-ON producer shared with
# gentest_codegen_public_module_imports.
gentest_require_public_module_producer()
set(_install_prefix "${_pm_install_prefix}")

set(_common_cache_args
  "-DCMAKE_MAKE_PROGRAM=${_ninja}"
  "-DCMAKE_C_COMPILER=${_clang}"
  "-DCMAKE_CXX_COMPILER=${_clangxx}"
  "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_target_scan_deps}")
gentest_resolve_fixture_build_type(_effective_build_type "${_clangxx}" "${BUILD_TYPE}")
if(NOT "${_effective_build_type}" STREQUAL "")
  list(APPEND _common_cache_args "-DCMAKE_BUILD_TYPE=${_effective_build_type}")
endif()
gentest_append_windows_native_llvm_cache_args(_common_cache_args "${_clangxx}" ${_common_cache_args})
gentest_append_host_apple_sysroot(_common_cache_args)

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
set(_sync_registration
  "${_consumer_build_dir}/gentest_codegen/tu_0001_sync_cases.registration.gentest.cpp")
gentest_expect_generated_boundary(
  "${_sync_registration}"
  "#include \"gentest/detail/registration_runtime.h\"")
file(READ "${_sync_registration}" _sync_registration_text)
foreach(_rejected IN ITEMS
    "#include \"gentest/async.h\""
    "#include \"gentest/detail/generated_runtime.h\"")
  string(FIND "${_sync_registration_text}" "${_rejected}" _sync_rejected_pos)
  if(NOT _sync_rejected_pos EQUAL -1)
    message(FATAL_ERROR
      "Simple synchronous module registration should not include '${_rejected}'.\n${_sync_registration_text}")
  endif()
endforeach()
set(_generated_header "${_consumer_build_dir}/gentest_codegen/tu_0000_cases.gentest.h")
gentest_expect_generated_boundary(
  "${_generated_header}"
  "#include \"gentest/detail/generated_runtime.h\"")
file(READ "${_generated_header}" _generated_header_text)
string(FIND "${_generated_header_text}" "#include \"gentest/async.h\"" _generated_async_pos)
if(_generated_async_pos EQUAL -1)
  message(FATAL_ERROR
    "Generated public-module registration header contains async cases and should require async support.\n${_generated_header_text}")
endif()

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
