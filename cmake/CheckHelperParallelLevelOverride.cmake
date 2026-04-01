# Proves that helper wrappers force their bounded nested parallel level even
# when the ambient shell exports larger values.

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(ENV{CMAKE_BUILD_PARALLEL_LEVEL} "77")
set(ENV{CTEST_PARALLEL_LEVEL} "88")
set(GENTEST_HELPER_BUILD_PARALLEL_LEVEL "1")

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" -E environment
  STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE _out)

string(FIND "${_out}" "CMAKE_BUILD_PARALLEL_LEVEL=1" _build_parallel_pos)
if(_build_parallel_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected helper wrapper to override CMAKE_BUILD_PARALLEL_LEVEL to 1.\nOutput:\n${_out}")
endif()

string(FIND "${_out}" "CTEST_PARALLEL_LEVEL=1" _ctest_parallel_pos)
if(_ctest_parallel_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected helper wrapper to override CTEST_PARALLEL_LEVEL to 1.\nOutput:\n${_out}")
endif()

message(STATUS "Helper parallel override regression passed")
