# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DGENERATOR=<cmake generator name>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleRegistrationSameModuleImpl.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleRegistrationSameModuleImpl.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENERATOR OR "${GENERATOR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleRegistrationSameModuleImpl.cmake: GENERATOR not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleRegistrationSameModuleImpl.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/module_registration_same_module_impl")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
gentest_remove_fixture_path("${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("clean module registration same-module impl regression: clang/clang++ not found")
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
  "-DCMAKE_CXX_COMPILER=${_clangxx}")
if(GENERATOR STREQUAL "Ninja" OR GENERATOR STREQUAL "Ninja Multi-Config")
  gentest_find_supported_ninja(_supported_ninja _supported_ninja_reason)
  if(NOT _supported_ninja)
    gentest_skip_test("clean module registration same-module impl regression: ${_supported_ninja_reason}")
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
gentest_find_clang_scan_deps(_clang_scan_deps "${_clangxx}")
if(NOT "${_clang_scan_deps}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_clang_scan_deps}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target module_registration_same_module_impl
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

file(GLOB _registration_headers "${_build_dir}/generated/*.gentest.h")
if(_registration_headers)
  string(JOIN "\n" _header_list ${_registration_headers})
  message(FATAL_ERROR
    "Clean module registration must not emit intermediate TU registration headers, but found:\n${_header_list}")
endif()

file(GLOB _registration_impls "${_build_dir}/generated/*.registration.gentest.cpp")
list(LENGTH _registration_impls _registration_impl_count)
if(NOT _registration_impl_count EQUAL 1)
  message(FATAL_ERROR
    "Expected exactly one additive registration implementation unit under '${_build_dir}/generated' for the selected module file set, found ${_registration_impl_count}")
endif()

file(GLOB _module_wrappers "${_build_dir}/generated/*.module.gentest.*")
if(_module_wrappers)
  string(JOIN "\n" _wrapper_list ${_module_wrappers})
  message(FATAL_ERROR
    "Clean module registration must not emit replacement module wrappers, but found:\n${_wrapper_list}")
endif()

list(GET _registration_impls 0 _registration_impl)
file(READ "${_registration_impl}" _registration_impl_text)
string(FIND "${_registration_impl_text}" "module gentest.module_registration_same_module_impl;" _module_decl_pos)
if(_module_decl_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected additive registration implementation '${_registration_impl}' to declare 'module gentest.module_registration_same_module_impl;'.")
endif()
string(FIND "${_registration_impl_text}" "register_cases(std::span{kCases})" _register_cases_pos)
if(_register_cases_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected additive registration implementation '${_registration_impl}' to contain direct registration code.")
endif()
string(REGEX MATCH "#include[ \t]+\"[^\"]+\\.gentest\\.h\"" _generated_header_include "${_registration_impl_text}")
if(NOT "${_generated_header_include}" STREQUAL "")
  message(FATAL_ERROR
    "Expected additive registration implementation '${_registration_impl}' to avoid including a generated TU registration header.\n"
    "Current contents:\n${_registration_impl_text}")
endif()

file(REMOVE "${_registration_impl}")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target module_registration_same_module_impl
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
if(NOT EXISTS "${_registration_impl}")
  message(FATAL_ERROR
    "Expected deleted additive registration implementation '${_registration_impl}' to be regenerated by the build.")
endif()

file(READ "${_src_dir}/cases.cppm" _module_source_text)
string(REPLACE "export module gentest.module_registration_same_module_impl;"
  "export module gentest.module_registration_same_module_impl_renamed;"
  _module_source_text
  "${_module_source_text}")
file(WRITE "${_src_dir}/cases.cppm" "${_module_source_text}")

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target module_registration_same_module_impl
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

file(READ "${_registration_impl}" _registration_impl_text)
string(FIND "${_registration_impl_text}" "module gentest.module_registration_same_module_impl_renamed;" _renamed_module_decl_pos)
if(_renamed_module_decl_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected additive registration implementation '${_registration_impl}' to refresh after renaming the source module.\n"
    "Current contents:\n${_registration_impl_text}")
endif()

set(_exe "${_build_dir}/module_registration_same_module_impl")
if(CMAKE_HOST_WIN32)
  set(_exe "${_exe}.exe")
endif()

gentest_check_run_or_fail(
  COMMAND "${_exe}" --list
  WORKING_DIRECTORY "${_build_dir}"
  OUTPUT_VARIABLE _list_out
  STRIP_TRAILING_WHITESPACE)
foreach(_expected_name IN ITEMS
    "module_registration_same_module_impl_ns/module/same_module_impl"
    "module_registration_same_module_impl_ns/module/same_module_impl_bench"
    "module_registration_same_module_impl_ns/module/same_module_impl_jitter")
  string(FIND "${_list_out}" "${_expected_name}" _list_pos)
  if(_list_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected clean module registration list output to contain '${_expected_name}'.\nCurrent output:\n${_list_out}")
  endif()
endforeach()

gentest_check_run_or_fail(
  COMMAND "${_exe}" --run=module/same_module_impl
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE)

gentest_check_run_or_fail(
  COMMAND "${_exe}" --run=module/same_module_impl_bench --kind=bench --bench-epochs=1 --bench-warmup=0 --bench-max-total-time-s=0.01
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE)

gentest_check_run_or_fail(
  COMMAND "${_exe}" --run=module/same_module_impl_jitter --kind=jitter --bench-epochs=1 --bench-warmup=0 --bench-max-total-time-s=0.01
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE)
