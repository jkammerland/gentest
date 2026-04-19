# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleRegistrationMockSplit.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleRegistrationMockSplit.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleRegistrationMockSplit.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

if(NOT GENERATOR STREQUAL "Ninja")
  gentest_skip_test("module registration mock split regression: MODULE_REGISTRATION requires a single-config Ninja fixture")
  return()
endif()

gentest_make_compact_fixture_work_dir(_work_dir
  PREFIX mmrs
  SOURCE_DIR "${SOURCE_DIR}"
  EXTRA_KEY "module_registration_mock_split")
set(_src_dir "${_work_dir}/s")
set(_build_dir "${_work_dir}/b")
gentest_remove_fixture_path("${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")
gentest_make_compact_fixture_source_link(_gentest_source_dir
  WORK_DIR "${_work_dir}"
  SOURCE_DIR "${GENTEST_SOURCE_DIR}"
  STEM "g")

gentest_resolve_clang_fixture_compilers(_effective_c_compiler _effective_cxx_compiler)
if(NOT _effective_c_compiler OR NOT _effective_cxx_compiler)
  gentest_skip_test("module registration mock split regression: no usable C/C++ compiler pair was provided")
  return()
endif()

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args
  "-DGENTEST_SOURCE_DIR=${_gentest_source_dir}"
  "-DCMAKE_C_COMPILER=${_effective_c_compiler}"
  "-DCMAKE_CXX_COMPILER=${_effective_cxx_compiler}")
if(GENERATOR STREQUAL "Ninja" OR GENERATOR STREQUAL "Ninja Multi-Config")
  gentest_find_supported_ninja(_supported_ninja _supported_ninja_reason)
  if(NOT _supported_ninja)
    gentest_skip_test("module registration mock split regression: ${_supported_ninja_reason}")
    return()
  endif()
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${_supported_ninja}")
elseif(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DClang_DIR=${Clang_DIR}")
endif()
if(DEFINED PROG AND NOT "${PROG}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}")
endif()
gentest_find_clang_scan_deps(_clang_scan_deps "${_effective_cxx_compiler}")
if(NOT "${_clang_scan_deps}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_clang_scan_deps}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()
gentest_append_public_modules_cache_arg(_cmake_cache_args)
gentest_append_windows_path_budget_cache_args(_cmake_cache_args)

message(STATUS "Configure module registration mock split fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Build module registration mock split fixture...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target module_registration_mock_split_tests
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_generated_dir "${_build_dir}/generated")
set(_mock_manifest "${_generated_dir}/module_registration_mock_split_tests.mock_manifest.json")
if(NOT EXISTS "${_mock_manifest}")
  message(FATAL_ERROR "Expected mock registration manifest '${_mock_manifest}'")
endif()
file(READ "${_mock_manifest}" _mock_manifest_text)
foreach(_token IN ITEMS
    "gentest.mock_manifest.v1"
    "story035_mock_split::Service"
    "gentest.story035.mock_split_provider")
  string(FIND "${_mock_manifest_text}" "${_token}" _manifest_token_pos)
  if(_manifest_token_pos EQUAL -1)
    message(FATAL_ERROR "Expected mock manifest token '${_token}'.\n${_mock_manifest_text}")
  endif()
endforeach()

file(GLOB _registration_units "${_generated_dir}/*.registration.gentest.cpp")
set(_provider_registration "")
foreach(_candidate IN LISTS _registration_units)
  file(READ "${_candidate}" _candidate_text)
  string(FIND "${_candidate_text}" "module gentest.story035.mock_split_provider;" _provider_module_pos)
  if(NOT _provider_module_pos EQUAL -1)
    set(_provider_registration "${_candidate}")
    set(_provider_registration_text "${_candidate_text}")
    break()
  endif()
endforeach()
if("${_provider_registration}" STREQUAL "")
  message(FATAL_ERROR "Expected a provider module registration unit.\nCandidates:\n${_registration_units}")
endif()

foreach(_token IN ITEMS
    "gentest_codegen: injected mock attachment"
    "mock<::story035_mock_split::Service>"
    "MockAccess<mock<::story035_mock_split::Service>>")
  string(FIND "${_provider_registration_text}" "${_token}" _registration_token_pos)
  if(_registration_token_pos EQUAL -1)
    message(FATAL_ERROR "Expected provider registration token '${_token}'.\n${_provider_registration_text}")
  endif()
endforeach()

foreach(_forbidden IN ITEMS
    "provider.cppm"
    "import gentest.story035.mock_split_provider")
  string(FIND "${_provider_registration_text}" "${_forbidden}" _forbidden_pos)
  if(NOT _forbidden_pos EQUAL -1)
    message(FATAL_ERROR "Provider registration unexpectedly contains '${_forbidden}'.\n${_provider_registration_text}")
  endif()
endforeach()

set(_exe "${_build_dir}/module_registration_mock_split_tests${CMAKE_EXECUTABLE_SUFFIX}")
gentest_check_run_or_fail(
  COMMAND "${_exe}" --run=module_registration_mock_split/module_owned_mock
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

gentest_cleanup_compact_fixture_source_link("${_gentest_source_dir}")
message(STATUS "Observed module registration mock split success")
