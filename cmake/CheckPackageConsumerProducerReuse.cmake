# Requires:
#  -DSOURCE_DIR=<path>
#  -DBUILD_ROOT=<path>
#  -DPACKAGE_NAME=<name>
#  -DPROG=<path to gentest_codegen>
# Optional:
#  -DGENERATOR=<cmake generator>
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain.cmake>
#  -DMAKE_PROGRAM=<make program>
#  -DC_COMPILER=<path>
#  -DCXX_COMPILER=<path>
#  -DPACKAGE_TEST_C_COMPILER=<path>
#  -DPACKAGE_TEST_CXX_COMPILER=<path>
#  -DPACKAGE_TEST_CXX_COMPILER_CLANG_SCAN_DEPS=<path>
#  -DBUILD_TYPE=<Debug|Release|...>
#  -DBUILD_CONFIG=<Debug|Release|...>
#  -DPACKAGE_TEST_USE_MODULES=<ON|OFF>            default OFF
#  -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=<ON|OFF>  default ON
#  -DCONSUMER_LINK_MODE=<main_only|runtime_only|double>  default main_only

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckPackageConsumerProducerReuse.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckPackageConsumerProducerReuse.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED PACKAGE_NAME OR "${PACKAGE_NAME}" STREQUAL "")
  message(FATAL_ERROR "CheckPackageConsumerProducerReuse.cmake: PACKAGE_NAME not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckPackageConsumerProducerReuse.cmake: PROG not set")
endif()
if(NOT DEFINED PACKAGE_TEST_USE_MODULES OR "${PACKAGE_TEST_USE_MODULES}" STREQUAL "")
  set(PACKAGE_TEST_USE_MODULES OFF)
endif()
if(NOT DEFINED PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE OR "${PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE}" STREQUAL "")
  set(PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE ON)
endif()
if(NOT DEFINED CONSUMER_LINK_MODE OR "${CONSUMER_LINK_MODE}" STREQUAL "")
  set(CONSUMER_LINK_MODE main_only)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

function(_gentest_extract_skip_marker input out_var)
  string(REGEX MATCH "GENTEST_SKIP_TEST:[^\r\n]*" _skip_marker "${input}")
  set(${out_var} "${_skip_marker}" PARENT_SCOPE)
endfunction()

if((NOT DEFINED PACKAGE_TEST_CXX_COMPILER_CLANG_SCAN_DEPS
    OR "${PACKAGE_TEST_CXX_COMPILER_CLANG_SCAN_DEPS}" STREQUAL "")
   AND DEFINED CXX_COMPILER_CLANG_SCAN_DEPS
   AND NOT "${CXX_COMPILER_CLANG_SCAN_DEPS}" STREQUAL "")
  set(PACKAGE_TEST_CXX_COMPILER_CLANG_SCAN_DEPS "${CXX_COMPILER_CLANG_SCAN_DEPS}")
endif()

set(_script "${CMAKE_CURRENT_LIST_DIR}/CheckPackageConsumer.cmake")
set(_proof_build_root "${BUILD_ROOT}/reuse-proof")
file(REMOVE_RECURSE "${_proof_build_root}")
set(_base_args
  "-DSOURCE_DIR=${SOURCE_DIR}"
  "-DBUILD_ROOT=${_proof_build_root}"
  "-DPACKAGE_NAME=${PACKAGE_NAME}"
  "-DPROG=${PROG}"
  "-DGENERATOR=${GENERATOR}"
  "-DGENERATOR_PLATFORM=${GENERATOR_PLATFORM}"
  "-DGENERATOR_TOOLSET=${GENERATOR_TOOLSET}"
  "-DTOOLCHAIN_FILE=${TOOLCHAIN_FILE}"
  "-DMAKE_PROGRAM=${MAKE_PROGRAM}"
  "-DC_COMPILER=${C_COMPILER}"
  "-DCXX_COMPILER=${CXX_COMPILER}"
  "-DPACKAGE_TEST_C_COMPILER=${PACKAGE_TEST_C_COMPILER}"
  "-DPACKAGE_TEST_CXX_COMPILER=${PACKAGE_TEST_CXX_COMPILER}"
  "-DPACKAGE_TEST_CXX_COMPILER_CLANG_SCAN_DEPS=${PACKAGE_TEST_CXX_COMPILER_CLANG_SCAN_DEPS}"
  "-DLLVM_DIR=${LLVM_DIR}"
  "-DClang_DIR=${Clang_DIR}"
  "-DBUILD_TYPE=${BUILD_TYPE}"
  "-DBUILD_CONFIG=${BUILD_CONFIG}"
  "-DPACKAGE_TEST_USE_MODULES=${PACKAGE_TEST_USE_MODULES}"
  "-DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=${PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE}"
  "-DCONSUMER_LINK_MODE=${CONSUMER_LINK_MODE}")

message(STATUS "Run first package-consumer pass...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" ${_base_args} -P "${_script}"
  STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE _first_out)
_gentest_extract_skip_marker("${_first_out}" _first_skip_marker)
if(NOT "${_first_skip_marker}" STREQUAL "")
  message(STATUS "${_first_skip_marker}")
  return()
endif()

string(FIND "${_first_out}" "Reuse cached producer build/install" _first_reuse_pos)
if(NOT _first_reuse_pos EQUAL -1)
  message(FATAL_ERROR
    "First package-consumer pass unexpectedly reported producer reuse.\nOutput:\n${_first_out}")
endif()

message(STATUS "Run second package-consumer pass...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" ${_base_args} -P "${_script}"
  STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE _second_out)
_gentest_extract_skip_marker("${_second_out}" _second_skip_marker)
if(NOT "${_second_skip_marker}" STREQUAL "")
  message(FATAL_ERROR
    "Second package-consumer pass unexpectedly skipped after a successful first pass.\nOutput:\n${_second_out}")
endif()

string(FIND "${_second_out}" "Reuse cached producer build/install" _second_reuse_pos)
if(_second_reuse_pos EQUAL -1)
  message(FATAL_ERROR
    "Second package-consumer pass did not report producer reuse.\nOutput:\n${_second_out}")
endif()
