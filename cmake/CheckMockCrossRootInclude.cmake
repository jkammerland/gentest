# Requires:
#  -DPROG=<path to gentest_codegen>
#  -DBUILD_ROOT=<build tree root>
#  -DPROJECT_SOURCE_DIR=<project source root>
#  -DTESTS_SOURCE_DIR=<tests source dir>
#  -DCODEGEN_STD=<std flag, e.g. -std=c++23>
# Optional:
#  -DCROSS_TARGET_ARG=<--target=...>
#
# Behavior:
#  - On Windows, if BUILD_ROOT and USERPROFILE are on different drives, this
#    script generates a mock target header on USERPROFILE drive and runs codegen
#    emit mode with outputs in BUILD_ROOT. This exercises cross-root include
#    path rendering in render_mocks().
#  - If both roots are the same, script reports a skip and succeeds.

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckMockCrossRootInclude.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckMockCrossRootInclude.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "CheckMockCrossRootInclude.cmake: PROJECT_SOURCE_DIR not set")
endif()
if(NOT DEFINED TESTS_SOURCE_DIR)
  message(FATAL_ERROR "CheckMockCrossRootInclude.cmake: TESTS_SOURCE_DIR not set")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckMockCrossRootInclude.cmake: CODEGEN_STD not set")
endif()

if(NOT WIN32)
  message(STATUS "CheckMockCrossRootInclude.cmake: non-Windows host; skipping")
  return()
endif()

set(_user_profile "$ENV{USERPROFILE}")
if(_user_profile STREQUAL "")
  message(FATAL_ERROR "CheckMockCrossRootInclude.cmake: USERPROFILE is empty")
endif()

file(TO_CMAKE_PATH "${BUILD_ROOT}" _build_root)
file(TO_CMAKE_PATH "${_user_profile}" _user_profile_norm)

string(REGEX MATCH "^[A-Za-z]:" _build_drive "${_build_root}")
string(REGEX MATCH "^[A-Za-z]:" _user_drive "${_user_profile_norm}")

if(_build_drive STREQUAL "" OR _user_drive STREQUAL "")
  message(FATAL_ERROR "CheckMockCrossRootInclude.cmake: unable to determine drive roots (build='${_build_root}', user='${_user_profile_norm}')")
endif()

if(_build_drive STREQUAL _user_drive)
  message(STATUS "CheckMockCrossRootInclude.cmake: build/user on same drive (${_build_drive}); skipping cross-root assertion")
  return()
endif()

set(_external_dir "${_user_profile_norm}/gentest_codegen_cross_root")
set(_external_header "${_external_dir}/cross_root_sink.hpp")
file(MAKE_DIRECTORY "${_external_dir}")
file(WRITE "${_external_header}" "namespace crossroot { struct Sink { void write(int) {} }; }\n")

set(_work_dir "${_build_root}/gentest_codegen_cross_root")
file(MAKE_DIRECTORY "${_work_dir}")
set(_input_cpp "${_work_dir}/cross_root_input.cpp")
set(_output_cpp "${_work_dir}/cross_root_output.gentest.cpp")
set(_mock_registry "${_work_dir}/cross_root_mock_registry.hpp")
set(_mock_impl "${_work_dir}/cross_root_mock_impl.hpp")

file(TO_CMAKE_PATH "${_external_header}" _external_header_norm)
file(WRITE "${_input_cpp}"
  "#include \"gentest/mock.h\"\n"
  "#include \"${_external_header_norm}\"\n"
  "using SinkMock = gentest::mock<crossroot::Sink>;\n"
  "[[maybe_unused]] inline SinkMock* kSinkMockPtr = nullptr;\n")

set(_args
  --output "${_output_cpp}"
  --mock-registry "${_mock_registry}"
  --mock-impl "${_mock_impl}"
  --compdb "${_build_root}"
  "${_input_cpp}"
  --)

if(DEFINED CROSS_TARGET_ARG AND NOT "${CROSS_TARGET_ARG}" STREQUAL "")
  list(APPEND _args "${CROSS_TARGET_ARG}")
endif()

list(APPEND _args
  "${CODEGEN_STD}"
  "-I${PROJECT_SOURCE_DIR}/include"
  "-I${TESTS_SOURCE_DIR}")

execute_process(
  COMMAND "${PROG}" ${_args}
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "Cross-root mock codegen failed (rc=${_rc}). Output:\n${_out}\nErrors:\n${_err}")
endif()

if(NOT EXISTS "${_mock_registry}")
  message(FATAL_ERROR "Cross-root mock codegen did not produce registry header: ${_mock_registry}")
endif()

file(READ "${_mock_registry}" _registry_text)
string(FIND "${_registry_text}" "#include \"${_external_header_norm}\"" _include_pos)
if(_include_pos EQUAL -1)
  message(FATAL_ERROR "Expected absolute include not found in registry header. Wanted '#include \"${_external_header_norm}\"'.")
endif()

message(STATUS "Cross-root mock include generation passed")
