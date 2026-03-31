# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
# Optional:
#  -DGENERATOR=<cmake generator name>
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<path>
#  -DMAKE_PROGRAM=<path>
#  -DC_COMPILER=<path>
#  -DCXX_COMPILER=<path>
#  -DBUILD_TYPE=<Debug|Release|...>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleManualPartialIncludes.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleManualPartialIncludes.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleManualPartialIncludes.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

function(_gentest_expect_partial_wrapper generated_dir module_name expected_include)
  file(GLOB _wrapper_candidates "${generated_dir}/*.module.gentest.cppm")
  set(_wrapper "")
  foreach(_candidate IN LISTS _wrapper_candidates)
    file(READ "${_candidate}" _candidate_text)
    string(FIND "${_candidate_text}" "export module ${module_name};" _module_pos)
    if(NOT _module_pos EQUAL -1)
      set(_wrapper "${_candidate}")
      set(_wrapper_text "${_candidate_text}")
      break()
    endif()
  endforeach()

  if("${_wrapper}" STREQUAL "")
    message(FATAL_ERROR
      "Expected a generated wrapper for module '${module_name}'.\n"
      "Candidates:\n${_wrapper_candidates}")
  endif()

  string(FIND "${_wrapper_text}" "${expected_include}" _manual_pos)
  string(FIND "${_wrapper_text}" "#include \"gentest/mock_codegen.h\"" _combined_pos)
  string(FIND "${_wrapper_text}" "export module ${module_name};" _module_pos)
  if(_manual_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected generated wrapper '${_wrapper}' to preserve '${expected_include}'.\n${_wrapper_text}")
  endif()
  if(_manual_pos GREATER _module_pos)
    message(FATAL_ERROR
      "Expected '${expected_include}' to stay in the global module fragment in '${_wrapper}'.\n${_wrapper_text}")
  endif()
  if(NOT _combined_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected generated wrapper '${_wrapper}' to avoid injecting gentest/mock_codegen.h when a partial manual include is present.\n${_wrapper_text}")
  endif()
endfunction()

set(_work_dir "${BUILD_ROOT}/module_manual_partial_includes")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("module manual partial include regression: clang/clang++ not found")
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
    gentest_skip_test("module manual partial include regression: ${_supported_ninja_reason}")
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
gentest_append_host_apple_sysroot(_cmake_cache_args)

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
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target manual_partial_tests
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_generated_dir "${_build_dir}/generated")
_gentest_expect_partial_wrapper("${_generated_dir}" "gentest.manual_partial_registry" "#include \"gentest/mock_registry_codegen.h\"")
_gentest_expect_partial_wrapper("${_generated_dir}" "gentest.manual_partial_impl" "#include \"gentest/mock_impl_codegen.h\"")

set(_exe_dir "${_build_dir}")
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  if(EXISTS "${_build_dir}/${BUILD_TYPE}/manual_partial_tests${CMAKE_EXECUTABLE_SUFFIX}")
    set(_exe_dir "${_build_dir}/${BUILD_TYPE}")
  endif()
endif()
set(_exe "${_exe_dir}/manual_partial_tests${CMAKE_EXECUTABLE_SUFFIX}")

gentest_check_run_or_fail(
  COMMAND "${_exe}" --list-tests
  WORKING_DIRECTORY "${_work_dir}"
  OUTPUT_VARIABLE _list_output
  STRIP_TRAILING_WHITESPACE)

foreach(_expected IN ITEMS
    "partial/manual_registry_include"
    "partial/manual_impl_include")
  string(FIND "${_list_output}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected partial manual include fixture to list '${_expected}'.\n"
      "Output:\n${_list_output}")
  endif()
endforeach()

message(STATUS "Module manual partial include regression passed")
