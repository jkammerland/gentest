# Requires:
#  -DPROG=<path to gentest_codegen executable>
#  -DBUILD_ROOT=<path to parent build dir>
# Optional:
#  -DTARGET_ARG=<optional --target=... argument>
#  -DCXX_COMPILER=<compiler for synthetic compile_commands entries>
#  -DEXPECT_SUBSTRING=<expected build error substring>

if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckTuHeaderCaseCollision.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckTuHeaderCaseCollision.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED EXPECT_SUBSTRING)
  set(EXPECT_SUBSTRING "multiple sources map to the same TU output header")
endif()

set(_work_dir "${BUILD_ROOT}/tu_header_case_collision")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}/generated")
set(_source_root "${_work_dir}")
set(_registry "${_work_dir}/mock_registry.hpp")
set(_impl "${_work_dir}/mock_impl.hpp")
set(_lower_src "${_work_dir}/lower_case.cpp")
set(_upper_src "${_work_dir}/Lower_Case.cpp")

file(WRITE "${_lower_src}" "int lower_case_fixture = 0;\n")
file(WRITE "${_upper_src}" "int upper_case_fixture = 0;\n")

set(_compdb_cxx "")
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  set(_compdb_cxx "${CXX_COMPILER}")
elseif(DEFINED ENV{CXX} AND NOT "$ENV{CXX}" STREQUAL "")
  set(_compdb_cxx "$ENV{CXX}")
else()
  set(_compdb_cxx "clang++")
endif()
set(_compdb_cxx_list "${_compdb_cxx}")
list(GET _compdb_cxx_list 0 _compdb_cxx)

set(_json_dir "${_work_dir}")
string(REPLACE "\\" "/" _json_dir "${_json_dir}")

set(_json_cxx "${_compdb_cxx}")
string(REPLACE "\\" "/" _json_cxx "${_json_cxx}")

set(_json_lower_fwd "${_lower_src}")
set(_json_upper_fwd "${_upper_src}")
string(REPLACE "\\" "/" _json_lower_fwd "${_json_lower_fwd}")
string(REPLACE "\\" "/" _json_upper_fwd "${_json_upper_fwd}")

set(_json_lower_native "${_lower_src}")
set(_json_upper_native "${_upper_src}")
if(WIN32)
  string(REPLACE "/" "\\" _json_lower_native "${_json_lower_native}")
  string(REPLACE "/" "\\" _json_upper_native "${_json_upper_native}")
endif()

foreach(_json_var IN ITEMS _json_lower_native _json_upper_native)
  string(REPLACE "\\" "\\\\" ${_json_var} "${${_json_var}}")
endforeach()

set(_compdb "${_work_dir}/compile_commands.json")
file(WRITE "${_compdb}"
  "[\n"
  "  {\n"
    "    \"directory\": \"${_json_dir}\",\n"
    "    \"file\": \"${_json_lower_fwd}\",\n"
    "    \"arguments\": [\"${_json_cxx}\", \"-c\", \"${_json_lower_fwd}\"]\n"
  "  },\n"
  "  {\n"
    "    \"directory\": \"${_json_dir}\",\n"
    "    \"file\": \"${_json_upper_fwd}\",\n"
    "    \"arguments\": [\"${_json_cxx}\", \"-c\", \"${_json_upper_fwd}\"]\n"
  "  },\n"
  "  {\n"
    "    \"directory\": \"${_json_dir}\",\n"
    "    \"file\": \"${_json_lower_native}\",\n"
    "    \"arguments\": [\"${_json_cxx}\", \"-c\", \"${_json_lower_native}\"]\n"
  "  },\n"
  "  {\n"
    "    \"directory\": \"${_json_dir}\",\n"
    "    \"file\": \"${_json_upper_native}\",\n"
    "    \"arguments\": [\"${_json_cxx}\", \"-c\", \"${_json_upper_native}\"]\n"
  "  }\n"
  "]\n")

set(_clang_args)
if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
  list(APPEND _clang_args "${TARGET_ARG}")
endif()
list(APPEND _clang_args -std=c++20)

set(_codegen_cmd
  "${PROG}"
  --mock-registry "${_registry}"
  --mock-impl "${_impl}"
  --tu-out-dir "${_work_dir}/generated"
  --compdb "${_work_dir}"
  --source-root "${_source_root}"
  "${_lower_src}"
  "${_upper_src}"
  --
  ${_clang_args})

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
