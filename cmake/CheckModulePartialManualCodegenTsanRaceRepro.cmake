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
#  -DTSAN_BUILD=<ON|OFF>
#  -DREPRO_TIMEOUT_SEC=<seconds>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModulePartialManualCodegenTsanRaceRepro.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModulePartialManualCodegenTsanRaceRepro.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModulePartialManualCodegenTsanRaceRepro.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED GENERATOR OR "${GENERATOR}" STREQUAL "")
  message(FATAL_ERROR "CheckModulePartialManualCodegenTsanRaceRepro.cmake: GENERATOR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

if(NOT DEFINED TSAN_BUILD OR NOT TSAN_BUILD)
  gentest_skip_test("TSAN codegen parallel-parse stress: top-level build is not ThreadSanitizer-instrumented")
  return()
endif()

set(_work_dir "${BUILD_ROOT}/module_partial_manual_codegen_tsan_race_repro")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("TSAN codegen parallel-parse stress: clang/clang++ not found")
  return()
endif()

if(NOT (GENERATOR STREQUAL "Ninja" OR GENERATOR STREQUAL "Ninja Multi-Config"))
  gentest_skip_test("TSAN codegen parallel-parse stress: Ninja generator required")
  return()
endif()

gentest_find_supported_ninja(_supported_ninja _supported_ninja_reason)
if(NOT _supported_ninja)
  gentest_skip_test("TSAN codegen parallel-parse stress: ${_supported_ninja_reason}")
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

set(_repro_timeout_sec 660)
if(DEFINED REPRO_TIMEOUT_SEC AND NOT "${REPRO_TIMEOUT_SEC}" STREQUAL "")
  set(_repro_timeout_sec "${REPRO_TIMEOUT_SEC}")
endif()

set(_tsan_options "halt_on_error=1:abort_on_error=1:second_deadlock_stack=1")
string(TIMESTAMP _start_sec "%s" UTC)
set(_iteration 0)
set(_saw_parallel_policy FALSE)
set(_elapsed_sec 0)

while(1)
  string(TIMESTAMP _now_sec "%s" UTC)
  math(EXPR _elapsed_sec "${_now_sec} - ${_start_sec}")
  if(_elapsed_sec GREATER_EQUAL _repro_timeout_sec)
    break()
  endif()

  math(EXPR _iteration "${_iteration} + 1")

  execute_process(
    COMMAND "${_supported_ninja}" -C "${_build_dir}" -t clean codegen
    RESULT_VARIABLE _clean_rc
    OUTPUT_VARIABLE _clean_out
    ERROR_VARIABLE _clean_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  if(NOT _clean_rc EQUAL 0)
    message(FATAL_ERROR
      "TSAN codegen parallel-parse stress: failed to clean codegen outputs before iteration ${_iteration}.\n"
      "--- stdout ---\n${_clean_out}\n--- stderr ---\n${_clean_err}")
  endif()

  execute_process(
    COMMAND
      "${CMAKE_COMMAND}" -E env
        GENTEST_CODEGEN_LOG_PARSE_POLICY=1
        TSAN_OPTIONS=${_tsan_options}
        "${CMAKE_COMMAND}" --build "${_build_dir}" --target codegen
    WORKING_DIRECTORY "${_work_dir}"
    RESULT_VARIABLE _build_rc
    OUTPUT_VARIABLE _build_out
    ERROR_VARIABLE _build_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  set(_all_output "${_build_out}\n${_build_err}")
  if(NOT _saw_parallel_policy)
    string(FIND "${_all_output}" "gentest_codegen: using multi-TU parse jobs=" _parallel_policy_pos)
    if(NOT _parallel_policy_pos EQUAL -1)
      set(_saw_parallel_policy TRUE)
    endif()
  endif()

  string(FIND "${_all_output}" "gentest_codegen: forcing serial multi-TU parse" _serial_policy_pos)
  if(NOT _serial_policy_pos EQUAL -1)
    message(FATAL_ERROR
      "TSAN codegen parallel-parse stress unexpectedly forced serial parse on iteration ${_iteration} after ${_elapsed_sec}s.\n"
      "--- output ---\n${_all_output}")
  endif()

  string(FIND "${_all_output}" "ThreadSanitizer: data race" _race_pos)
  string(FIND "${_all_output}" "TrackingStatistic::RegisterStatistic" _stat_pos)
  if(NOT _race_pos EQUAL -1 AND NOT _stat_pos EQUAL -1)
    message(FATAL_ERROR
      "TSAN codegen parallel-parse stress reproduced the TrackingStatistic::RegisterStatistic race on iteration ${_iteration} after ${_elapsed_sec}s.\n"
      "--- output ---\n${_all_output}")
  endif()

  if(_build_rc EQUAL 0)
    continue()
  endif()

  message(FATAL_ERROR
    "TSAN codegen parallel-parse stress hit an unexpected failure on iteration ${_iteration} after ${_elapsed_sec}s.\n"
    "--- output ---\n${_all_output}")
endwhile()

if(NOT _saw_parallel_policy)
  message(FATAL_ERROR
    "TSAN codegen parallel-parse stress never confirmed that parallel parse stayed enabled.\n"
    "Iterations: ${_iteration}\nElapsed: ${_elapsed_sec}s")
endif()

message(STATUS
  "TSAN codegen parallel-parse stress stayed clean for ${_elapsed_sec}s across ${_iteration} iterations")
