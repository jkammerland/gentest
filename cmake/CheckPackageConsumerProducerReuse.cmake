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

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

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
  "-DPACKAGE_TEST_USE_MODULES=OFF"
  "-DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON"
  "-DCONSUMER_LINK_MODE=main_only")

message(STATUS "Run first package-consumer pass...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" ${_base_args} -P "${_script}"
  STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE _first_out)

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

string(FIND "${_second_out}" "Reuse cached producer build/install" _second_reuse_pos)
if(_second_reuse_pos EQUAL -1)
  message(FATAL_ERROR
    "Second package-consumer pass did not report producer reuse.\nOutput:\n${_second_out}")
endif()
