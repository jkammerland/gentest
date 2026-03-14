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
  message(FATAL_ERROR "CheckModuleManualIncludeWhitespaceDuplicate.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleManualIncludeWhitespaceDuplicate.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleManualIncludeWhitespaceDuplicate.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/module_manual_include_whitespace")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("module manual include whitespace regression: clang/clang++ not found")
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
    gentest_skip_test("module manual include whitespace regression: ${_supported_ninja_reason}")
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

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _configure_rc
  OUTPUT_VARIABLE _configure_out
  ERROR_VARIABLE _configure_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _configure_rc EQUAL 0)
  message(FATAL_ERROR "Configure failed unexpectedly.\n${_configure_out}\n${_configure_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target manual_include_whitespace_tests
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE _build_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR "Build failed unexpectedly.\n${_build_out}\n${_build_err}")
endif()

file(GLOB _wrapper_candidates "${_build_dir}/generated/*.module.gentest.cppm")
list(LENGTH _wrapper_candidates _wrapper_count)
if(NOT _wrapper_count EQUAL 1)
  message(FATAL_ERROR "Expected exactly one generated module wrapper, found ${_wrapper_count}: ${_wrapper_candidates}")
endif()
list(GET _wrapper_candidates 0 _wrapper)
file(READ "${_wrapper}" _wrapper_text)
string(FIND "${_wrapper_text}" "export module gentest.manual_include_whitespace;" _module_pos)
string(FIND "${_wrapper_text}" "// gentest_codegen: injected mock codegen include." _auto_codegen_include_pos)
string(FIND "${_wrapper_text}" "#include \"gentest/mock_codegen.h\"" _canonical_include_pos)
string(FIND "${_wrapper_text}" "# include \"gentest/mock_codegen.h\"" _manual_spaced_pos)

if(_module_pos EQUAL -1)
  message(FATAL_ERROR "Expected generated wrapper to contain the named module declaration.\n${_wrapper_text}")
endif()
if(_auto_codegen_include_pos EQUAL -1 OR _canonical_include_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected generated wrapper to normalize the manual mock_codegen include into the injected global-fragment block in ${_wrapper}.\n${_wrapper_text}")
endif()
if(_auto_codegen_include_pos GREATER _module_pos OR _canonical_include_pos GREATER _module_pos)
  message(FATAL_ERROR
    "Expected mock_codegen support to be injected before the named module declaration in ${_wrapper}.\n${_wrapper_text}")
endif()
if(NOT _manual_spaced_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected generated wrapper to relocate the spaced manual include out of module purview in ${_wrapper}.\n${_wrapper_text}")
endif()

set(_exe_dir "${_build_dir}")
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  if(EXISTS "${_build_dir}/${BUILD_TYPE}/manual_include_whitespace_tests${CMAKE_EXECUTABLE_SUFFIX}")
    set(_exe_dir "${_build_dir}/${BUILD_TYPE}")
  endif()
endif()
set(_exe "${_exe_dir}/manual_include_whitespace_tests${CMAKE_EXECUTABLE_SUFFIX}")

execute_process(
  COMMAND "${_exe}" --list-tests
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _list_rc
  OUTPUT_VARIABLE _list_out
  ERROR_VARIABLE _list_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _list_rc EQUAL 0)
  message(FATAL_ERROR "Listing tests failed unexpectedly.\n${_list_out}\n${_list_err}")
endif()
if(NOT _list_out MATCHES "whitespace/manual_spaced_include")
  message(FATAL_ERROR "Expected manual include whitespace test to be discovered.\n${_list_out}\n${_list_err}")
endif()

execute_process(
  COMMAND "${_exe}" --run=whitespace/manual_spaced_include
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _run_rc
  OUTPUT_VARIABLE _run_out
  ERROR_VARIABLE _run_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _run_rc EQUAL 0)
  message(FATAL_ERROR "Running discovered test failed unexpectedly.\n${_run_out}\n${_run_err}")
endif()

message(STATUS "Module manual include whitespace regression passed")
