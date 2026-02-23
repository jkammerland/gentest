# Requires:
#  -DSOURCE_DIR=<path to fixture project>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENERATOR=<cmake generator name>
#  -DPROG=<path to gentest_codegen executable>
# Optional:
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain>
#  -DMAKE_PROGRAM=<make/ninja path>
#  -DC_COMPILER=<C compiler>
#  -DCXX_COMPILER=<C++ compiler>
#  -DBUILD_TYPE=<Debug/Release/...>
#  -DTARGET_ARG=<optional --target=... argument>
#  -DEXPECT_SUBSTRING=<expected build error substring>

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckTuHeaderCaseCollision.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckTuHeaderCaseCollision.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENERATOR)
  message(FATAL_ERROR "CheckTuHeaderCaseCollision.cmake: GENERATOR not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckTuHeaderCaseCollision.cmake: PROG not set")
endif()
if(NOT DEFINED EXPECT_SUBSTRING)
  set(EXPECT_SUBSTRING "multiple sources map to the same TU output header")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_work_dir "${BUILD_ROOT}/tu_header_case_collision")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_build_dir "${_work_dir}/build")
set(_generated_dir "${_work_dir}/generated")
file(MAKE_DIRECTORY "${_generated_dir}")

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

message(STATUS "Configure gentest_tu_header_case_collision fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}"
)

set(_compdb "${_build_dir}/compile_commands.json")
if(NOT EXISTS "${_compdb}")
  message(FATAL_ERROR "Expected CMake to generate '${_compdb}', but it does not exist")
endif()

set(_build_cmd
  "${CMAKE_COMMAND}"
  --build "${_build_dir}"
  --target case_collision_obj)
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _build_cmd --config "${BUILD_TYPE}")
endif()

message(STATUS "Build gentest_tu_header_case_collision fixture target...")
gentest_check_run_or_fail(
  COMMAND
    ${_build_cmd}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}"
)

set(_registry "${_work_dir}/mock_registry.hpp")
set(_impl "${_work_dir}/mock_impl.hpp")
set(_lower_src "${_build_dir}/a/lower_case.cpp")
set(_upper_src "${_build_dir}/b/Lower_Case.cpp")

set(_clang_args)
if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
  list(APPEND _clang_args "${TARGET_ARG}")
endif()

set(_codegen_cmd
  "${PROG}"
  --mock-registry "${_registry}"
  --mock-impl "${_impl}"
  --tu-out-dir "${_generated_dir}"
  --compdb "${_build_dir}"
  --source-root "${_build_dir}"
  "${_lower_src}"
  "${_upper_src}")
if(_clang_args)
  list(APPEND _codegen_cmd --)
  list(APPEND _codegen_cmd ${_clang_args})
endif()

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
