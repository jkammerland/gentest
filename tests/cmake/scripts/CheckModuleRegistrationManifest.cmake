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

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleRegistrationManifest.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleRegistrationManifest.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENERATOR OR "${GENERATOR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleRegistrationManifest.cmake: GENERATOR not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleRegistrationManifest.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

if(NOT GENERATOR STREQUAL "Ninja")
  gentest_skip_test("module registration manifest regression: MODULE_REGISTRATION requires a single-config Ninja fixture")
  return()
endif()

set(_work_dir "${BUILD_ROOT}/module_registration_manifest")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("module registration manifest regression: clang/clang++ not found")
  return()
endif()

gentest_find_supported_ninja(_supported_ninja _supported_ninja_reason)
if(NOT _supported_ninja)
  gentest_skip_test("module registration manifest regression: ${_supported_ninja_reason}")
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
  "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}"
  "-DCMAKE_C_COMPILER=${_clang}"
  "-DCMAKE_CXX_COMPILER=${_clangxx}"
  "-DCMAKE_MAKE_PROGRAM=${_supported_ninja}")
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
gentest_find_clang_scan_deps(_clang_scan_deps "${_clangxx}")
if(NOT "${_clang_scan_deps}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_clang_scan_deps}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()
gentest_append_public_modules_cache_arg(_cmake_cache_args)

message(STATUS "Configure module registration manifest fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Build module registration manifest fixture...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target module_registration_manifest_tests
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_exe "${_build_dir}/module_registration_manifest_tests")
if(CMAKE_HOST_WIN32)
  set(_exe "${_exe}.exe")
endif()

gentest_check_run_or_fail(
  COMMAND "${_exe}" --list-tests
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE _list_out)
string(FIND "${_list_out}" "module_registration/non_exported_fixture" _case_pos)
if(_case_pos EQUAL -1)
  message(FATAL_ERROR "Expected module_registration/non_exported_fixture in --list-tests output.\n${_list_out}")
endif()

gentest_check_run_or_fail(
  COMMAND "${_exe}" --run=module_registration/non_exported_fixture
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_registration_source "${_build_dir}/generated/tu_0000_cases.registration.gentest.cpp")
if(NOT EXISTS "${_registration_source}")
  message(FATAL_ERROR "Expected generated registration source '${_registration_source}'")
endif()
file(READ "${_registration_source}" _registration_text)
foreach(_required IN ITEMS
    "module gentest.story034.module_registration;"
    "#include \"tu_0000_cases.gentest.h\"")
  string(FIND "${_registration_text}" "${_required}" _required_pos)
  if(_required_pos EQUAL -1)
    message(FATAL_ERROR "Generated registration source is missing '${_required}'.\n${_registration_text}")
  endif()
endforeach()
foreach(_forbidden IN ITEMS
    "import gentest.story034.module_registration"
    "cases.cppm")
  string(FIND "${_registration_text}" "${_forbidden}" _forbidden_pos)
  if(NOT _forbidden_pos EQUAL -1)
    message(FATAL_ERROR "Generated registration source must not contain '${_forbidden}'.\n${_registration_text}")
  endif()
endforeach()

set(_manifest "${_build_dir}/generated/module_registration_manifest_tests.artifact_manifest.json")
if(NOT EXISTS "${_manifest}")
  message(FATAL_ERROR "Expected generated artifact manifest '${_manifest}'")
endif()
file(READ "${_manifest}" _manifest_json)
string(JSON _manifest_schema GET "${_manifest_json}" schema)
string(JSON _source_kind GET "${_manifest_json}" sources 0 kind)
string(JSON _source_module GET "${_manifest_json}" sources 0 module)
string(JSON _source_context GET "${_manifest_json}" sources 0 compile_context_id)
string(JSON _source_registration GET "${_manifest_json}" sources 0 registration_output)
string(JSON _artifact_path GET "${_manifest_json}" artifacts 0 path)
string(JSON _artifact_compile_as GET "${_manifest_json}" artifacts 0 compile_as)
string(JSON _artifact_module GET "${_manifest_json}" artifacts 0 module)
string(JSON _artifact_context GET "${_manifest_json}" artifacts 0 compile_context_id)
string(JSON _artifact_scan GET "${_manifest_json}" artifacts 0 requires_module_scan)
set(_expected_context "module_registration_manifest_tests:${_src_dir}/cases.cppm")

foreach(_actual_expected IN ITEMS
    "_manifest_schema=gentest.artifact_manifest.v1"
    "_source_kind=module-primary-interface"
    "_source_module=gentest.story034.module_registration"
    "_source_context=${_expected_context}"
    "_source_registration=${_registration_source}"
    "_artifact_path=${_registration_source}"
    "_artifact_compile_as=cxx-module-implementation"
    "_artifact_module=gentest.story034.module_registration"
    "_artifact_context=${_expected_context}"
    "_artifact_scan=ON")
  string(REPLACE "=" ";" _pair "${_actual_expected}")
  list(GET _pair 0 _actual_var)
  list(GET _pair 1 _expected_value)
  if(NOT "${${_actual_var}}" STREQUAL "${_expected_value}")
    message(FATAL_ERROR "Manifest mismatch for ${_actual_var}: expected '${_expected_value}', got '${${_actual_var}}'.\n${_manifest_json}")
  endif()
endforeach()

set(_validation_stamp "${_build_dir}/generated/module_registration_manifest_tests.artifact_manifest.validated")
if(NOT EXISTS "${_validation_stamp}")
  message(FATAL_ERROR "Expected manifest validation stamp '${_validation_stamp}'")
endif()

set(_compdb "${_build_dir}/compile_commands.json")
if(NOT EXISTS "${_compdb}")
  message(FATAL_ERROR "Expected compile_commands.json at '${_compdb}'")
endif()
file(READ "${_compdb}" _compdb_json)
string(REPLACE "\\\\" "/" _compdb_json_normalized "${_compdb_json}")
string(REPLACE "\\" "/" _compdb_json_normalized "${_compdb_json_normalized}")
foreach(_expected_entry IN ITEMS
    "${_src_dir}/cases.cppm"
    "${_registration_source}")
  set(_expected_entry_normalized "${_expected_entry}")
  string(REPLACE "\\\\" "/" _expected_entry_normalized "${_expected_entry_normalized}")
  string(REPLACE "\\" "/" _expected_entry_normalized "${_expected_entry_normalized}")
  string(FIND "${_compdb_json_normalized}" "${_expected_entry_normalized}" _entry_pos)
  if(_entry_pos EQUAL -1)
    message(FATAL_ERROR "Expected compile_commands.json to retain '${_expected_entry}' after path normalization.\n${_compdb_json}")
  endif()
endforeach()

function(_gentest_expect_manifest_validation_failure expected_substring)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target gentest_codegen_module_registration_manifest_tests
    WORKING_DIRECTORY "${_work_dir}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  if(_rc EQUAL 0)
    message(FATAL_ERROR "Expected manifest validation rebuild to fail after corrupting the manifest")
  endif()
  set(_combined "${_out}\n${_err}")
  string(FIND "${_combined}" "${expected_substring}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected manifest validation failure to contain '${expected_substring}'.\n--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()
endfunction()

set(_bad_registration_source "${_build_dir}/generated/not_declared.registration.gentest.cpp")
string(JSON _bad_manifest_json SET "${_manifest_json}" artifacts 0 path "\"${_bad_registration_source}\"")
file(WRITE "${_manifest}" "${_bad_manifest_json}\n")
file(REMOVE "${_validation_stamp}")
_gentest_expect_manifest_validation_failure("artifacts[0].path mismatch")

string(JSON _bad_manifest_json SET "${_manifest_json}" sources 0 module "\"gentest.story034.wrong_module\"")
string(JSON _bad_manifest_json SET "${_bad_manifest_json}" artifacts 0 module "\"gentest.story034.wrong_module\"")
file(WRITE "${_manifest}" "${_bad_manifest_json}\n")
file(REMOVE "${_validation_stamp}")
_gentest_expect_manifest_validation_failure("sources[0].module mismatch")

message(STATUS "Module registration manifest regression passed")
