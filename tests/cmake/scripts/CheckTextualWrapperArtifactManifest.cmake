# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DGENERATOR=<cmake generator name>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckTextualWrapperArtifactManifest.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckTextualWrapperArtifactManifest.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENERATOR OR "${GENERATOR}" STREQUAL "")
  message(FATAL_ERROR "CheckTextualWrapperArtifactManifest.cmake: GENERATOR not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckTextualWrapperArtifactManifest.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

if(NOT GENERATOR STREQUAL "Ninja")
  gentest_skip_test("textual wrapper artifact manifest regression: fixture uses a single-config Ninja build")
  return()
endif()

set(_work_dir "${BUILD_ROOT}/textual_wrapper_artifact_manifest")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("textual wrapper artifact manifest regression: clang/clang++ not found")
  return()
endif()

gentest_find_supported_ninja(_supported_ninja _supported_ninja_reason)
if(NOT _supported_ninja)
  gentest_skip_test("textual wrapper artifact manifest regression: ${_supported_ninja_reason}")
  return()
endif()

set(_cmake_gen_args -G "${GENERATOR}")
set(_cmake_cache_args
  "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}"
  "-DCMAKE_C_COMPILER=${_clang}"
  "-DCMAKE_CXX_COMPILER=${_clangxx}"
  "-DCMAKE_MAKE_PROGRAM=${_supported_ninja}")
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DClang_DIR=${Clang_DIR}")
endif()
if(DEFINED PROG AND NOT "${PROG}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" ${_cmake_gen_args} -S "${_src_dir}" -B "${_build_dir}" ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target textual_wrapper_artifact_manifest_tests
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_exe "${_build_dir}/textual_wrapper_artifact_manifest_tests")
if(CMAKE_HOST_WIN32)
  set(_exe "${_exe}.exe")
endif()

gentest_check_run_or_fail(
  COMMAND "${_exe}" --list-tests
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE _list_out)
foreach(_expected_case IN ITEMS
    "textual_manifest/anonymous_static"
    "textual_manifest/local_fixture"
    "textual_manifest/second_source")
  string(FIND "${_list_out}" "${_expected_case}" _case_pos)
  if(_case_pos EQUAL -1)
    message(FATAL_ERROR "Expected '${_expected_case}' in --list-tests output.\n${_list_out}")
  endif()
endforeach()

foreach(_case_name IN ITEMS
    "textual_manifest/anonymous_static"
    "textual_manifest/local_fixture"
    "textual_manifest/second_source")
  gentest_check_run_or_fail(
    COMMAND "${_exe}" "--run=${_case_name}"
    WORKING_DIRECTORY "${_build_dir}"
    STRIP_TRAILING_WHITESPACE)
endforeach()

set(_owner_source_0 "${_src_dir}/cases.cpp")
set(_wrapper_source_0 "${_build_dir}/generated/tu_0000_cases.gentest.cpp")
set(_registration_header_0 "${_build_dir}/generated/tu_0000_cases.gentest.h")
set(_owner_source_1 "${_src_dir}/more_cases.cpp")
set(_wrapper_source_1 "${_build_dir}/generated/tu_0001_more_cases.gentest.cpp")
set(_registration_header_1 "${_build_dir}/generated/tu_0001_more_cases.gentest.h")
set(_manifest "${_build_dir}/generated/textual_wrapper_artifact_manifest_tests.artifact_manifest.json")
set(_depfile "${_build_dir}/generated/textual_wrapper_artifact_manifest_tests.gentest.d")
set(_validation_stamp "${_build_dir}/generated/textual_wrapper_artifact_manifest_tests.artifact_manifest.validated")

foreach(_expected_file IN ITEMS
    "${_wrapper_source_0}"
    "${_registration_header_0}"
    "${_wrapper_source_1}"
    "${_registration_header_1}"
    "${_manifest}"
    "${_depfile}"
    "${_validation_stamp}")
  if(NOT EXISTS "${_expected_file}")
    message(FATAL_ERROR "Expected generated file '${_expected_file}'")
  endif()
endforeach()

function(_gentest_expect_wrapper wrapper_source owner_include registration_include)
  file(READ "${wrapper_source}" _wrapper_text)
  foreach(_required IN ITEMS
      "#include \"${owner_include}\""
      "#include \"${registration_include}\"")
    string(FIND "${_wrapper_text}" "${_required}" _required_pos)
    if(_required_pos EQUAL -1)
      message(FATAL_ERROR "Generated textual wrapper '${wrapper_source}' is missing '${_required}'.\n${_wrapper_text}")
    endif()
  endforeach()
endfunction()

_gentest_expect_wrapper("${_wrapper_source_0}" "../../src/cases.cpp" "tu_0000_cases.gentest.h")
_gentest_expect_wrapper("${_wrapper_source_1}" "../../src/more_cases.cpp" "tu_0001_more_cases.gentest.h")

file(READ "${_manifest}" _manifest_json)
string(JSON _source_count LENGTH "${_manifest_json}" sources)
string(JSON _artifact_count LENGTH "${_manifest_json}" artifacts)
if(NOT _source_count EQUAL 2 OR NOT _artifact_count EQUAL 2)
  message(FATAL_ERROR "Expected two textual manifest sources/artifacts, got sources=${_source_count}, artifacts=${_artifact_count}.\n${_manifest_json}")
endif()

function(_gentest_expect_manifest_entry idx owner_source wrapper_source registration_header)
  string(JSON _source_path GET "${_manifest_json}" sources ${idx} source)
  string(JSON _source_kind GET "${_manifest_json}" sources ${idx} kind)
  string(JSON _source_owner GET "${_manifest_json}" sources ${idx} owner_source)
  string(JSON _source_wrapper GET "${_manifest_json}" sources ${idx} generated_wrapper_source)
  string(JSON _source_header GET "${_manifest_json}" sources ${idx} registration_header)
  string(JSON _artifact_path GET "${_manifest_json}" artifacts ${idx} path)
  string(JSON _artifact_role GET "${_manifest_json}" artifacts ${idx} role)
  string(JSON _artifact_compile_as GET "${_manifest_json}" artifacts ${idx} compile_as)
  string(JSON _artifact_owner GET "${_manifest_json}" artifacts ${idx} owner_source)
  string(JSON _artifact_wrapper GET "${_manifest_json}" artifacts ${idx} generated_wrapper_source)
  string(JSON _artifact_attachment GET "${_manifest_json}" artifacts ${idx} target_attachment)
  string(JSON _artifact_scan GET "${_manifest_json}" artifacts ${idx} requires_module_scan)
  string(JSON _artifact_includes_owner GET "${_manifest_json}" artifacts ${idx} includes_owner_source)
  string(JSON _artifact_replaces_owner GET "${_manifest_json}" artifacts ${idx} replaces_owner_source)
  string(JSON _artifact_header GET "${_manifest_json}" artifacts ${idx} generated_headers 0)
  string(JSON _artifact_depfile GET "${_manifest_json}" artifacts ${idx} depfile)

  foreach(_actual_expected IN ITEMS
      "_source_path=${wrapper_source}"
      "_source_kind=textual-wrapper"
      "_source_owner=${owner_source}"
      "_source_wrapper=${wrapper_source}"
      "_source_header=${registration_header}"
      "_artifact_path=${wrapper_source}"
      "_artifact_role=registration"
      "_artifact_compile_as=cxx-textual-wrapper"
      "_artifact_owner=${owner_source}"
      "_artifact_wrapper=${wrapper_source}"
      "_artifact_attachment=replace-owner-source"
      "_artifact_scan=OFF"
      "_artifact_includes_owner=ON"
      "_artifact_replaces_owner=ON"
      "_artifact_header=${registration_header}"
      "_artifact_depfile=${_depfile}")
    string(REPLACE "=" ";" _pair "${_actual_expected}")
    list(GET _pair 0 _actual_var)
    list(GET _pair 1 _expected_value)
    if(NOT "${${_actual_var}}" STREQUAL "${_expected_value}")
      message(FATAL_ERROR "Manifest mismatch for ${_actual_var}: expected '${_expected_value}', got '${${_actual_var}}'.\n${_manifest_json}")
    endif()
  endforeach()
endfunction()

_gentest_expect_manifest_entry(0 "${_owner_source_0}" "${_wrapper_source_0}" "${_registration_header_0}")
_gentest_expect_manifest_entry(1 "${_owner_source_1}" "${_wrapper_source_1}" "${_registration_header_1}")

message(STATUS "Textual wrapper artifact manifest regression passed")
