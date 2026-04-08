if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenSmokeEmission.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenSmokeEmission.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenSmokeEmission.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED SMOKE_SOURCE OR "${SMOKE_SOURCE}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenSmokeEmission.cmake: SMOKE_SOURCE not set")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenSmokeEmission.cmake: CODEGEN_STD not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_compdb_root "${BUILD_ROOT}")
if(DEFINED COMPDB_ROOT AND NOT "${COMPDB_ROOT}" STREQUAL "")
  set(_compdb_root "${COMPDB_ROOT}")
endif()

get_filename_component(_smoke_abs "${SOURCE_DIR}/${SMOKE_SOURCE}" ABSOLUTE)
if(NOT EXISTS "${_smoke_abs}")
  message(FATAL_ERROR "CheckCodegenSmokeEmission.cmake: missing smoke source '${_smoke_abs}'")
endif()

get_filename_component(_smoke_name "${SMOKE_SOURCE}" NAME_WE)
set(_work_dir "${BUILD_ROOT}/codegen_smoke_emission/${_smoke_name}")
set(_output "${_work_dir}/${_smoke_name}.gentest.cpp")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_codegen_args
  --output "${_output}"
  --compdb "${_compdb_root}"
  "${_smoke_abs}"
  --)
if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
  list(APPEND _codegen_args "${TARGET_ARG}")
endif()
list(APPEND _codegen_args
  "${CODEGEN_STD}"
  "-I${SOURCE_DIR}/include"
  "-I${SOURCE_DIR}/tests")

gentest_check_run_or_fail(
  COMMAND
    "${PROG}"
    ${_codegen_args}
  STRIP_TRAILING_WHITESPACE)

if(NOT EXISTS "${_output}")
  message(FATAL_ERROR "Expected generated output '${_output}' to exist")
endif()

file(READ "${_output}" _output_text)

if(DEFINED EXPECT_CASE_COUNT AND NOT "${EXPECT_CASE_COUNT}" STREQUAL "")
  set(_case_needle "constexpr std::array<gentest::Case, ${EXPECT_CASE_COUNT}> kCases = {")
  string(FIND "${_output_text}" "${_case_needle}" _case_pos)
  if(_case_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected generated output for '${SMOKE_SOURCE}' to contain '${_case_needle}'.\n"
      "Generated file: ${_output}\n${_output_text}")
  endif()
endif()

foreach(_expected IN LISTS EXPECT_SUBSTRINGS)
  string(FIND "${_output_text}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected generated output for '${SMOKE_SOURCE}' to contain '${_expected}'.\n"
      "Generated file: ${_output}\n${_output_text}")
  endif()
endforeach()
