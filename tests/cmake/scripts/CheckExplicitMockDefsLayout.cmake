# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
# Optional:
#  -DGENERATOR=<cmake generator name>
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain.cmake>
#  -DMAKE_PROGRAM=<path>
#  -DBUILD_TYPE=<Debug|Release|...>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckExplicitMockDefsLayout.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckExplicitMockDefsLayout.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckExplicitMockDefsLayout.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")
include("${GENTEST_SOURCE_DIR}/cmake/gentest/CodegenToolchain.cmake")

if(GENERATOR MATCHES "Ninja Multi-Config|Visual Studio|Xcode")
  gentest_skip_test("explicit mock DEFS layout regression: explicit mock targets currently require a single-config generator")
  return()
endif()
if(NOT GENERATOR STREQUAL "Ninja")
  gentest_skip_test("explicit mock DEFS layout regression: the build-order assertions require the Ninja generator")
  return()
endif()

if(CMAKE_HOST_WIN32)
  set(_work_dir "${BUILD_ROOT}/emdl")
else()
  set(_work_dir "${BUILD_ROOT}/explicit_mock_defs_layout")
endif()
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)

if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("explicit mock DEFS layout regression: no usable clang/clang++ pair was provided")
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
  "-DCMAKE_CXX_COMPILER=${_clangxx}")
if(GENERATOR STREQUAL "Ninja" OR GENERATOR STREQUAL "Ninja Multi-Config")
  gentest_find_supported_ninja(_supported_ninja _supported_ninja_reason)
  if(NOT _supported_ninja)
    gentest_skip_test("explicit mock DEFS layout regression: ${_supported_ninja_reason}")
    return()
  endif()
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${_supported_ninja}")
elseif(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
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
gentest_append_public_modules_cache_arg(_cmake_cache_args)
gentest_append_host_apple_sysroot(_cmake_cache_args)
include("${CMAKE_CURRENT_LIST_DIR}/FixturePrefixContext.cmake")
gentest_fixture_prefix_cache_args(_prefix_cache_args _cmake_cache_args)
list(APPEND _cmake_cache_args ${_prefix_cache_args})

set(_install_dir "${_work_dir}/install")

function(_gentest_expect_contains label content expected)
  string(FIND "${content}" "${expected}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "${label}: expected '${expected}'.\n--- content ---\n${content}")
  endif()
endfunction()

function(_gentest_expect_excludes label content unexpected)
  string(FIND "${content}" "${unexpected}" _pos)
  if(NOT _pos EQUAL -1)
    message(FATAL_ERROR "${label}: did not expect '${unexpected}'.\n--- content ---\n${content}")
  endif()
endfunction()

message(STATUS "Configure explicit mock DEFS layout fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

# Mock codegen reads headers that the build writes: one from a custom command of
# a linked library, one that is the DEFS file itself. The codegen step must be
# ordered after both. Ask Ninja what each generated registry waits for...
_gentest_make_codegen_target_id("gen_dep_mocks" _gen_dep_mocks_id)
_gentest_make_codegen_target_id("built_mocks" _built_mocks_id)
foreach(_ordering IN ITEMS
    "generated/gen_dep/${_gen_dep_mocks_id}_mock_registry.hpp|gen_dep_api"
    "generated/built/${_built_mocks_id}_mock_registry.hpp|built/built_mocks.hpp")
  string(REPLACE "|" ";" _ordering "${_ordering}")
  list(GET _ordering 0 _codegen_output)
  list(GET _ordering 1 _expected_input)
  gentest_check_run_or_fail(
    COMMAND "${_supported_ninja}" -C "${_build_dir}" -t query "${_codegen_output}"
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE _codegen_edge)
  string(FIND "${_codegen_edge}" "outputs:" _codegen_edge_outputs_pos)
  string(SUBSTRING "${_codegen_edge}" 0 ${_codegen_edge_outputs_pos} _codegen_edge_inputs)
  _gentest_expect_contains(
    "The codegen step writing '${_codegen_output}' must wait for '${_expected_input}'"
    "${_codegen_edge_inputs}" "${_expected_input}")
endforeach()

# ...and build only the consumers, from clean and in parallel. The generating
# commands sleep, so codegen that is not ordered after them finds no header.
message(STATUS "Build the consumers of generated mock inputs from clean...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --parallel 4 --target gen_dep_consumer built_consumer
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Build explicit mock DEFS layout fixture...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

foreach(_consumer IN ITEMS
    "same_name_consumer|same_name"
    "split_consumer|split"
    "gen_dep_consumer|generated_dependency"
    "built_consumer|built_defs")
  string(REPLACE "|" ";" _consumer "${_consumer}")
  list(GET _consumer 0 _consumer_exe)
  list(GET _consumer 1 _consumer_case)
  message(STATUS "Run ${_consumer_exe}...")
  gentest_check_run_or_fail(
    COMMAND "${_build_dir}/${_consumer_exe}${CMAKE_EXECUTABLE_SUFFIX}" "--run=explicit_mock_defs_layout/${_consumer_case}"
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)
endforeach()

# Same-named DEFS of two targets keep their directories, so the spellings differ.
file(READ "${_build_dir}/generated/a/public/a_mocks.hpp" _a_public_header)
file(READ "${_build_dir}/generated/b/public/b_mocks.hpp" _b_public_header)
_gentest_expect_contains("a_mocks public header" "${_a_public_header}" "#include <a/mocks.hpp>")
_gentest_expect_contains("b_mocks public header" "${_b_public_header}" "#include <b/mocks.hpp>")

# BASE_DIRS names the include root explicitly.
file(READ "${_build_dir}/generated/gen_dep/public/gen_dep_mocks.hpp" _gen_dep_public_header)
_gentest_expect_contains("gen_dep_mocks public header" "${_gen_dep_public_header}" "#include <gen_dep/mocks.hpp>")

# DEFS in the source and build trees: each is spelled from its own tree. The
# directory holding both trees, and the build directory's name, stay out of the
# generated header, the include directories and the installed layout.
get_filename_component(_build_dir_name "${_build_dir}" NAME)
get_filename_component(_src_dir_name "${_src_dir}" NAME)
file(READ "${_build_dir}/generated/split/public/split_mocks.hpp" _split_public_header)
_gentest_expect_contains("split_mocks public header" "${_split_public_header}" "#include <split/authored_mocks.hpp>")
_gentest_expect_contains("split_mocks public header" "${_split_public_header}" "#include <split_generated/generated_mocks.hpp>")
_gentest_expect_excludes("split_mocks public header" "${_split_public_header}" "${_build_dir_name}/")
_gentest_expect_excludes("split_mocks public header" "${_split_public_header}" "${_work_dir}")

file(STRINGS "${_build_dir}/split_mocks_include_dirs.txt" _split_include_dirs)
set(_split_saw_src_dir FALSE)
set(_split_saw_build_dir FALSE)
foreach(_split_include_dir IN LISTS _split_include_dirs)
  string(REGEX REPLACE "^\\$<BUILD_INTERFACE:(.*)>$" "\\1" _split_include_dir "${_split_include_dir}")
  if(_split_include_dir STREQUAL "" OR _split_include_dir MATCHES "\\$<")
    continue()
  endif()
  cmake_path(SET _split_include_dir NORMALIZE "${_split_include_dir}")
  string(REGEX REPLACE "/+$" "" _split_include_dir "${_split_include_dir}")
  if(_split_include_dir STREQUAL _src_dir)
    set(_split_saw_src_dir TRUE)
  elseif(_split_include_dir STREQUAL _build_dir)
    set(_split_saw_build_dir TRUE)
  endif()
  cmake_path(IS_PREFIX _split_include_dir "${_src_dir}" _split_contains_src_dir)
  if(_split_contains_src_dir AND NOT _split_include_dir STREQUAL _src_dir)
    message(FATAL_ERROR
      "split_mocks exposes '${_split_include_dir}', a parent of its source directory, as an include directory.\n"
      "${_split_include_dirs}")
  endif()
endforeach()
if(NOT _split_saw_src_dir OR NOT _split_saw_build_dir)
  message(FATAL_ERROR
    "Expected split_mocks to expose '${_src_dir}' and '${_build_dir}' as include directories.\n${_split_include_dirs}")
endif()

message(STATUS "Install the split mock target...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --install "${_build_dir}" --prefix "${_install_dir}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
foreach(_installed_rel IN ITEMS
    public/split_mocks.hpp
    split/authored_mocks.hpp
    split/authored.hpp
    split_generated/generated_mocks.hpp)
  if(NOT EXISTS "${_install_dir}/include/${_installed_rel}")
    message(FATAL_ERROR "Expected installed '${_install_dir}/include/${_installed_rel}'")
  endif()
endforeach()
file(GLOB_RECURSE _installed_headers RELATIVE "${_install_dir}/include" "${_install_dir}/include/*")
foreach(_installed_header IN LISTS _installed_headers)
  if(_installed_header MATCHES "(^|/)(${_build_dir_name}|${_src_dir_name})/")
    message(FATAL_ERROR "Installed header path '${_installed_header}' contains a build or source directory name")
  endif()
  file(READ "${_install_dir}/include/${_installed_header}" _installed_header_content)
  _gentest_expect_excludes("Installed '${_installed_header}'" "${_installed_header_content}" "${_work_dir}")
endforeach()
file(READ "${_install_dir}/include/split/authored_mocks.hpp" _installed_authored_defs)
file(READ "${_src_dir}/split/authored_mocks.hpp" _authored_defs)
if(NOT _installed_authored_defs STREQUAL _authored_defs)
  message(FATAL_ERROR "Installed DEFS 'split/authored_mocks.hpp' differ from the authored file")
endif()

# Two targets that spell different DEFS files alike, linked without a gentest
# CMake helper: the generated headers must refuse to compile together.
message(STATUS "Build a consumer of two mock targets with clashing DEFS include paths...")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target clash_consumer
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _clash_rc
  OUTPUT_VARIABLE _clash_out
  ERROR_VARIABLE _clash_err)
if(_clash_rc EQUAL 0)
  message(FATAL_ERROR "Expected clash_consumer to fail to compile.\n${_clash_out}\n${_clash_err}")
endif()
_gentest_expect_contains("clash_consumer build output" "${_clash_out}\n${_clash_err}"
  "another explicit mock target already includes a different DEFS file as <mocks.hpp>")
