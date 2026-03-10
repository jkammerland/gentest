# Requires:
#  -DPROG=<path to gentest_codegen>
#  -DBUILD_ROOT=<build tree root>
#  -DPROJECT_SOURCE_DIR=<project source root>
#  -DCODEGEN_STD=<std flag, e.g. -std=c++23>
# Optional:
#  -DCROSS_TARGET_ARG=<--target=...>
#
# Behavior:
#  - On non-Windows hosts, this script creates a real source tree plus a
#    symlinked view of that tree and verifies generated mock registry headers
#    keep the symlink-visible include path instead of canonicalizing to the
#    real host path.

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckMockSymlinkInclude.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckMockSymlinkInclude.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "CheckMockSymlinkInclude.cmake: PROJECT_SOURCE_DIR not set")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckMockSymlinkInclude.cmake: CODEGEN_STD not set")
endif()

if(WIN32)
  message(STATUS "CheckMockSymlinkInclude.cmake: Windows host covered by cross-root mock include test; skipping")
  return()
endif()

set(_work_dir "${BUILD_ROOT}/gentest_codegen_symlink_include")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_real_root "${_work_dir}/real")
set(_view_root "${_work_dir}/view")
set(_real_include_dir "${_real_root}/include")
set(_real_src_dir "${_real_root}/src")
file(MAKE_DIRECTORY "${_real_include_dir}" "${_real_src_dir}")

set(_real_header "${_real_include_dir}/symlink_sink.hpp")
set(_input_cpp "${_real_src_dir}/input.cpp")
set(_output_cpp "${_work_dir}/symlink_output.gentest.cpp")
set(_mock_registry "${_work_dir}/symlink_mock_registry.hpp")
set(_mock_impl "${_work_dir}/symlink_mock_impl.hpp")

file(WRITE "${_real_header}" "namespace symlinkprobe { struct Sink { void write(int) {} }; }\n")
file(WRITE "${_input_cpp}"
  "#include \"gentest/mock.h\"\n"
  "#include \"../include/symlink_sink.hpp\"\n"
  "using SinkMock = gentest::mock<symlinkprobe::Sink>;\n"
  "[[maybe_unused]] inline SinkMock* kSinkMockPtr = nullptr;\n")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E create_symlink "${_real_root}" "${_view_root}"
  RESULT_VARIABLE _symlink_rc
  OUTPUT_VARIABLE _symlink_out
  ERROR_VARIABLE _symlink_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _symlink_rc EQUAL 0)
  message(FATAL_ERROR "Failed to create symlinked source tree (rc=${_symlink_rc}). Output:\n${_symlink_out}\nErrors:\n${_symlink_err}")
endif()

set(_args
  --output "${_output_cpp}"
  --mock-registry "${_mock_registry}"
  --mock-impl "${_mock_impl}"
  "${_view_root}/src/input.cpp"
  --)

if(DEFINED CROSS_TARGET_ARG AND NOT "${CROSS_TARGET_ARG}" STREQUAL "")
  list(APPEND _args "${CROSS_TARGET_ARG}")
endif()

set(_codegen_std "${CODEGEN_STD}")
if(_codegen_std MATCHES "^/std:c\\+\\+([0-9]+)$")
  set(_codegen_std "-std=c++${CMAKE_MATCH_1}")
elseif(_codegen_std STREQUAL "/std:c++latest")
  set(_codegen_std "-std=c++23")
endif()

list(APPEND _args
  -x
  c++
  "${_codegen_std}"
  "-I${PROJECT_SOURCE_DIR}/include"
  "-I${_view_root}/include")

execute_process(
  COMMAND "${PROG}" ${_args}
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "Symlink mock codegen failed (rc=${_rc}). Output:\n${_out}\nErrors:\n${_err}")
endif()

if(NOT EXISTS "${_mock_registry}")
  message(FATAL_ERROR "Symlink mock codegen did not produce registry header: ${_mock_registry}")
endif()

file(READ "${_mock_registry}" _registry_text)
set(_expected_include "#include \"view/include/symlink_sink.hpp\"")
set(_forbidden_include "#include \"real/include/symlink_sink.hpp\"")
string(FIND "${_registry_text}" "${_expected_include}" _expected_pos)
if(_expected_pos EQUAL -1)
  message(FATAL_ERROR "Expected symlink-preserving include not found in registry header. Wanted '${_expected_include}'.")
endif()

string(FIND "${_registry_text}" "${_forbidden_include}" _forbidden_pos)
if(NOT _forbidden_pos EQUAL -1)
  message(FATAL_ERROR "Registry header leaked real source-tree path via '${_forbidden_include}'.")
endif()

message(STATUS "Symlink mock include generation passed")
