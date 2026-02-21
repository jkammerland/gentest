# Requires:
#  -DSOURCE_DIR=<path to fixture sources>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DCODEGEN_EXECUTABLE=<path to gentest_codegen executable>
# Optional:
#  -DEXPECT_SUBSTRING=<expected build error substring>

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckTuHeaderCaseCollision.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckTuHeaderCaseCollision.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED CODEGEN_EXECUTABLE OR "${CODEGEN_EXECUTABLE}" STREQUAL "")
  message(FATAL_ERROR "CheckTuHeaderCaseCollision.cmake: CODEGEN_EXECUTABLE not set")
endif()
if(NOT DEFINED EXPECT_SUBSTRING)
  set(EXPECT_SUBSTRING "multiple sources map to the same TU output header")
endif()

set(_work_dir "${BUILD_ROOT}/tu_header_case_collision")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}/generated")
get_filename_component(_repo_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_registry "${_work_dir}/mock_registry.hpp")
set(_impl "${_work_dir}/mock_impl.hpp")

set(_codegen_cmd
  "${CODEGEN_EXECUTABLE}"
  --mock-registry "${_registry}"
  --mock-impl "${_impl}"
  --tu-out-dir "${_work_dir}/generated"
  --source-root "${_repo_root}"
  "${SOURCE_DIR}/lower_case.cpp"
  "${SOURCE_DIR}/Lower_Case.cpp"
  --
  -std=c++20
  -I${_repo_root}/include
  -I${SOURCE_DIR})

execute_process(
  COMMAND ${_codegen_cmd}
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE _build_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}"
)

set(_all "${_build_out}\n${_build_err}")
if(_build_rc EQUAL 0)
  message(FATAL_ERROR "Expected build to fail due to TU header case-collision, but exit code was 0. Output:\n${_all}")
endif()

string(FIND "${_all}" "${EXPECT_SUBSTRING}" _pos)
if(_pos EQUAL -1)
  message(FATAL_ERROR "Expected substring not found in build output: '${EXPECT_SUBSTRING}'. Output:\n${_all}")
endif()

message(STATUS "TU header case-collision check passed (build failed with expected message)")
