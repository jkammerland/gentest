# Requires the standard _gentest_add_cmake_helper_test variables.

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BUILD_ROOT OR NOT DEFINED GENTEST_SOURCE_DIR OR NOT DEFINED GENERATOR)
  message(FATAL_ERROR "CheckHeaderDeclarationRegistration.cmake: required helper variable missing")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

if(NOT GENERATOR STREQUAL "Ninja")
  gentest_skip_test("header-declaration registration regression: fixture uses a single-config Ninja build")
  return()
endif()

set(_work_dir "${BUILD_ROOT}/header_declaration_registration")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("header-declaration registration regression: clang/clang++ not found")
  return()
endif()
gentest_find_supported_ninja(_supported_ninja _supported_ninja_reason)
if(NOT _supported_ninja)
  gentest_skip_test("header-declaration registration regression: ${_supported_ninja_reason}")
  return()
endif()

set(_cache_args
  "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}"
  "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}"
  "-DCMAKE_C_COMPILER=${_clang}"
  "-DCMAKE_CXX_COMPILER=${_clangxx}"
  "-DCMAKE_MAKE_PROGRAM=${_supported_ninja}")
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _cache_args "-DClang_DIR=${Clang_DIR}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" -G Ninja -S "${_src_dir}" -B "${_build_dir}" ${_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target header_declaration_registration_tests
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_exe "${_build_dir}/header_declaration_registration_tests")
if(CMAKE_HOST_WIN32)
  set(_exe "${_exe}.exe")
endif()
gentest_check_run_or_fail(
  COMMAND "${_exe}" --list-tests
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE _list_output)
foreach(_name IN ITEMS "header_declaration/a" "header_declaration/b")
  string(FIND "${_list_output}" "${_name}" _name_pos)
  if(_name_pos EQUAL -1)
    message(FATAL_ERROR "Expected '${_name}' in --list-tests output.\n${_list_output}")
  endif()
  gentest_check_run_or_fail(COMMAND "${_exe}" "--run=${_name}" WORKING_DIRECTORY "${_build_dir}" STRIP_TRAILING_WHITESPACE)
endforeach()

set(_registration_a "${_build_dir}/generated/tu_0000_cases_a.header_registration.gentest.cpp")
set(_registration_b "${_build_dir}/generated/tu_0001_cases_b.header_registration.gentest.cpp")
set(_registration_c "${_build_dir}/generated/tu_0002_cases_c.header_registration.gentest.cpp")
foreach(_file IN ITEMS "${_registration_a}" "${_registration_b}" "${_registration_c}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "Expected generated registration source '${_file}'")
  endif()
endforeach()
file(READ "${_registration_a}" _registration_a_text)
file(READ "${_registration_b}" _registration_b_text)
foreach(_header IN ITEMS "header_a.hpp")
  string(FIND "${_registration_a_text}" "${_header}" _header_pos)
  if(_header_pos EQUAL -1)
    message(FATAL_ERROR "Assigned registration source is missing '${_header}'.\n${_registration_a_text}")
  endif()
endforeach()
file(READ "${_registration_b}" _registration_b_text)
foreach(_header IN ITEMS "header_b.hpp")
  string(FIND "${_registration_b_text}" "${_header}" _header_pos)
  if(_header_pos EQUAL -1)
    message(FATAL_ERROR "Assigned registration source is missing '${_header}'.\n${_registration_b_text}")
  endif()
endforeach()
foreach(_source IN ITEMS "cases_a.cpp" "cases_b.cpp")
  string(FIND "${_registration_a_text}" "${_source}" _source_pos)
  if(NOT _source_pos EQUAL -1)
    message(FATAL_ERROR "Generated registration source must not include authored source '${_source}'.\n${_registration_a_text}")
  endif()
endforeach()
file(READ "${_registration_c}" _registration_c_text)
string(FIND "${_registration_c_text}" "No gentest registrations were assigned" _empty_pos)
if(_empty_pos EQUAL -1)
  message(FATAL_ERROR "Unassigned slot must emit a valid empty registration source.\n${_registration_c_text}")
endif()

file(READ "${_build_dir}/compile_commands.json" _compdb)
foreach(_source IN ITEMS "cases_a.cpp" "cases_b.cpp" "cases_c.cpp")
  string(FIND "${_compdb}" "\"file\": \"${_src_dir}/${_source}\"" _compdb_source_pos)
  if(_compdb_source_pos EQUAL -1)
    message(FATAL_ERROR "Authored source '${_source}' must retain its direct compilation-database entry.\n${_compdb}")
  endif()
endforeach()

set(_cpp_only_case "${_work_dir}/cpp_only_case.cpp")
file(WRITE "${_cpp_only_case}" [=[
#include <gentest/runner.h>

[[using gentest: test("header_declaration/cpp_only")]]
void cpp_only_case() {}
]=])
execute_process(
  COMMAND "${PROG}"
    --tu-out-dir "${_work_dir}/cpp_only_generated"
    --textual-registration-output "${_work_dir}/cpp_only_generated/registration.cpp"
    "${_cpp_only_case}"
    --
    -std=c++20
    "-I${GENTEST_SOURCE_DIR}/include"
  RESULT_VARIABLE _cpp_only_rc
  OUTPUT_VARIABLE _cpp_only_stdout
  ERROR_VARIABLE _cpp_only_stderr)
if(_cpp_only_rc EQUAL 0)
  message(FATAL_ERROR "A Gentest attribute in an authored .cpp must be rejected by header-declaration registration.\n${_cpp_only_stdout}\n${_cpp_only_stderr}")
endif()
string(FIND "${_cpp_only_stdout}${_cpp_only_stderr}" "annotate the header declaration instead" _cpp_only_diag_pos)
if(_cpp_only_diag_pos EQUAL -1)
  message(FATAL_ERROR "Expected focused .cpp annotation diagnostic.\n${_cpp_only_stdout}\n${_cpp_only_stderr}")
endif()

message(STATUS "Header-declaration registration regression passed")
