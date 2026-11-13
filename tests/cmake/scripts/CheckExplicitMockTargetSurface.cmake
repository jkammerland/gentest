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
  message(FATAL_ERROR "CheckExplicitMockTargetSurface.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckExplicitMockTargetSurface.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckExplicitMockTargetSurface.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")
include("${GENTEST_SOURCE_DIR}/cmake/gentest/CodegenToolchain.cmake")

if(GENERATOR MATCHES "Ninja Multi-Config|Visual Studio|Xcode")
  gentest_skip_test("explicit mock target surface regression: explicit mock targets currently require a single-config generator")
  return()
endif()
if(NOT GENERATOR STREQUAL "Ninja")
  gentest_skip_test("explicit mock target surface regression: explicit module mock targets currently require the Ninja generator")
  return()
endif()

if(CMAKE_HOST_WIN32)
  set(_work_dir "${BUILD_ROOT}/emts")
else()
  set(_work_dir "${BUILD_ROOT}/explicit_mock_target_surface")
endif()
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)

if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("explicit mock target surface regression: no usable clang/clang++ pair was provided")
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
    gentest_skip_test("explicit mock target surface regression: ${_supported_ninja_reason}")
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

message(STATUS "Configure explicit mock target surface fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Build explicit mock target surface fixture...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

foreach(_generated_header IN ITEMS
    "${_build_dir}/generated/header/public/fixture_header_mocks.hpp"
    "${_build_dir}/generated/header_extra/public/fixture_alt_header_mocks.hpp")
  if(NOT EXISTS "${_generated_header}")
    message(FATAL_ERROR "Expected generated explicit mock header was not written: ${_generated_header}")
  endif()
endforeach()

# DEFS are used in place: nothing is copied into the generated tree, and the
# public header reaches the authored DEFS through the DEFS file-set root.
foreach(_copied_defs_dir IN ITEMS
    "${_build_dir}/generated/header/defs"
    "${_build_dir}/generated/header_extra/defs"
    "${_build_dir}/generated/module/defs"
    "${_build_dir}/generated/module/deps")
  if(EXISTS "${_copied_defs_dir}")
    message(FATAL_ERROR "Explicit mock DEFS must not be copied, but found '${_copied_defs_dir}'")
  endif()
endforeach()
file(READ "${_build_dir}/generated/header/public/fixture_header_mocks.hpp" _public_header_content)
foreach(_expected_include IN ITEMS "#include \"header_mocks.hpp\"" "#include \"aggregate_header_mocks.hpp\"")
  string(FIND "${_public_header_content}" "${_expected_include}" _expected_include_pos)
  if(_expected_include_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected the generated public header to include the authored DEFS via '${_expected_include}'.\n${_public_header_content}")
  endif()
endforeach()

# #239: production headers that rely on #pragma once alone, included next to
# the generated mock surface in both orders, with real expectations.
foreach(_pragma_once_order IN ITEMS production_first mock_first)
  gentest_check_run_or_fail(
    COMMAND
      "${_build_dir}/pragma_once_${_pragma_once_order}${CMAKE_EXECUTABLE_SUFFIX}"
      "--run=explicit_mock_target/pragma_once_${_pragma_once_order}"
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)
endforeach()
# Generated registries spell the mocked types' headers the way consumers reach
# them, so the same spelling works in the build tree and after installation.
file(GLOB _pragma_once_registries "${_build_dir}/generated/pragma_once/*_mock_registry__domain_0000_header.hpp")
if(NOT _pragma_once_registries)
  message(FATAL_ERROR "Expected a generated registry domain header under '${_build_dir}/generated/pragma_once'")
endif()
list(GET _pragma_once_registries 0 _pragma_once_registry)
file(READ "${_pragma_once_registry}" _pragma_once_registry_content)
foreach(_expected_include IN ITEMS "#include \"ledger.hpp\"" "#include \"app/account.hpp\"")
  string(FIND "${_pragma_once_registry_content}" "${_expected_include}" _expected_include_pos)
  if(_expected_include_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected '${_expected_include}' in ${_pragma_once_registry}.\n${_pragma_once_registry_content}")
  endif()
endforeach()

set(_generated_module_header "${_build_dir}/generated/module/public/fixture_module_mocks.hpp")
if(EXISTS "${_generated_module_header}")
  message(FATAL_ERROR
    "Did not expect a generated explicit mock public header for module defs, but found: ${_generated_module_header}")
endif()
file(GLOB _generated_module_public_headers "${_build_dir}/generated/module/public/*")
if(_generated_module_public_headers)
  message(FATAL_ERROR
    "Did not expect any generated module public headers under '${_build_dir}/generated/module/public', but found: "
    "${_generated_module_public_headers}")
endif()

file(GLOB _generated_module_root_headers "${_build_dir}/generated/module/*.hpp")
_gentest_make_codegen_target_id("explicit_module_mocks" _explicit_module_mocks_id)
foreach(_generated_module_root_header IN LISTS _generated_module_root_headers)
  get_filename_component(_generated_module_root_name "${_generated_module_root_header}" NAME)
  if(NOT _generated_module_root_name MATCHES "^${_explicit_module_mocks_id}_mock_(impl|registry)(|__domain_.*)\\.hpp$")
    message(FATAL_ERROR
      "Expected module explicit mocks to emit only internal support headers at the output root, but found unexpected header "
      "'${_generated_module_root_header}'.")
  endif()
endforeach()

file(GLOB _generated_module_wrappers "${_build_dir}/generated/module/tu_*.module.gentest.cppm")
set(_generated_module_service_wrappers)
set(_generated_module_mock_surface_wrappers)
foreach(_generated_module_wrapper IN LISTS _generated_module_wrappers)
  file(READ "${_generated_module_wrapper}" _generated_module_wrapper_content)
  if(_generated_module_wrapper_content MATCHES "(^|[\r\n])export module fixture\\.service_module;")
    list(APPEND _generated_module_service_wrappers "${_generated_module_wrapper}")
  endif()
  if(_generated_module_wrapper_content MATCHES "(^|[\r\n])export module fixture\\.module_mocks;")
    list(APPEND _generated_module_mock_surface_wrappers "${_generated_module_wrapper}")
  endif()
endforeach()
list(LENGTH _generated_module_service_wrappers _generated_module_service_wrapper_count)
list(LENGTH _generated_module_mock_surface_wrappers _generated_module_mock_surface_wrapper_count)
if(NOT _generated_module_service_wrapper_count EQUAL 1)
  message(FATAL_ERROR
    "Expected exactly one generated explicit mock provider wrapper for 'fixture.service_module', but found: "
    "${_generated_module_service_wrappers}")
endif()
if(NOT _generated_module_mock_surface_wrapper_count EQUAL 1)
  message(FATAL_ERROR
    "Expected exactly one generated explicit mock surface wrapper for 'fixture.module_mocks', but found: "
    "${_generated_module_mock_surface_wrappers}")
endif()
set(_generated_module_aggregate "${_build_dir}/generated/module/fixture/explicit_module_mocks.cppm")
if(NOT EXISTS "${_generated_module_aggregate}")
  message(FATAL_ERROR "Expected generated explicit mock aggregate module was not written: ${_generated_module_aggregate}")
endif()

foreach(_prog_name IN ITEMS explicit_header_consumer explicit_module_consumer)
  set(_prog_path "${_build_dir}/${_prog_name}${CMAKE_EXECUTABLE_SUFFIX}")
  message(STATUS "Run ${_prog_name}...")
  gentest_check_run_or_fail(
    COMMAND "${_prog_path}"
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)
endforeach()

message(STATUS "Run explicit_late_link_consumer...")
gentest_check_run_or_fail(
  COMMAND
    "${_build_dir}/explicit_late_link_consumer${CMAKE_EXECUTABLE_SUFFIX}"
    "--run=explicit_mock_target/late_link_consumer"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Run explicit_late_link_module_consumer...")
gentest_check_run_or_fail(
  COMMAND
    "${_build_dir}/explicit_late_link_module_consumer${CMAKE_EXECUTABLE_SUFFIX}"
    "--run=explicit_mock_target/late_link_module_consumer"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

# Reconfiguring an unchanged project must not regenerate the mocks. Run the
# native build so restat can discard unchanged compilation-database copies.
message(STATUS "Reconfigure explicit mock targets without changing inputs...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" -S "${_src_dir}" -B "${_build_dir}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE _unchanged_reconfigure_output)
if(_unchanged_reconfigure_output MATCHES "Running gentest_codegen|Inspecting gentest mocks|Building CXX object")
  message(FATAL_ERROR
    "Reconfiguring unchanged explicit mock targets caused unnecessary regeneration or compilation.\n"
    "Build output:\n${_unchanged_reconfigure_output}")
endif()

message(STATUS "Mutate local support header and rebuild explicit header consumer...")
file(READ "${_src_dir}/service.hpp" _service_header_content)
string(REPLACE "kServiceSentinel = 9" "kServiceSentinel = 11" _service_header_content "${_service_header_content}")
file(WRITE "${_src_dir}/service.hpp" "${_service_header_content}")
file(READ "${_src_dir}/fixture/a+b.hpp" _regex_meta_header_content)
string(REPLACE
  "kRegexMetaHeaderSentinel = 17"
  "kRegexMetaHeaderSentinel = 23"
  _regex_meta_header_content
  "${_regex_meta_header_content}")
file(WRITE "${_src_dir}/fixture/a+b.hpp" "${_regex_meta_header_content}")

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE _support_header_rebuild_output)
# The consumer includes the authored support header directly, so editing it
# rebuilds the consumer, and no reconfigure is needed: nothing is copied.
if(NOT _support_header_rebuild_output MATCHES "header_consumer")
  message(FATAL_ERROR
    "Editing service.hpp did not rebuild explicit_header_consumer.\nBuild output:\n${_support_header_rebuild_output}")
endif()
if(_support_header_rebuild_output MATCHES "Re-running CMake")
  message(FATAL_ERROR
    "Editing an explicit mock support header must not reconfigure.\nBuild output:\n${_support_header_rebuild_output}")
endif()

gentest_check_run_or_fail(
  COMMAND "${_build_dir}/explicit_header_consumer${CMAKE_EXECUTABLE_SUFFIX}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Mutate explicit module mock defs and rebuild late-linked module consumer...")
file(READ "${_src_dir}/module_mocks.cppm" _module_mocks_content)
string(FIND "${_module_mocks_content}" "\r\n" _module_mocks_has_crlf)
if(NOT _module_mocks_has_crlf EQUAL -1)
  string(REPLACE "\r\n" "\n" _module_mocks_content "${_module_mocks_content}")
endif()
string(REPLACE
  "export import fixture.service_module;\n"
  "export import fixture.service_module;\n\n// late-link module rebuild marker\n"
  _module_mocks_content
  "${_module_mocks_content}")
if(NOT _module_mocks_has_crlf EQUAL -1)
  string(REPLACE "\n" "\r\n" _module_mocks_content "${_module_mocks_content}")
endif()
file(WRITE "${_src_dir}/module_mocks.cppm" "${_module_mocks_content}")

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target explicit_late_link_module_consumer
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE _late_link_module_rebuild_output)

string(FIND "${_late_link_module_rebuild_output}" "Running gentest_codegen for target explicit_late_link_module_consumer"
  _late_link_module_codegen_pos)
if(_late_link_module_codegen_pos EQUAL -1)
  message(FATAL_ERROR
    "Incremental rebuild after editing explicit module mock defs did not rerun codegen for "
    "'explicit_late_link_module_consumer'.\n"
    "Build output:\n${_late_link_module_rebuild_output}")
endif()

gentest_check_run_or_fail(
  COMMAND
    "${_build_dir}/explicit_late_link_module_consumer${CMAKE_EXECUTABLE_SUFFIX}"
    "--run=explicit_mock_target/late_link_module_consumer"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

# A content change in the compilation database must still invalidate discovery.
message(STATUS "Change a compile definition and discover the newly enabled case...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" -S "${_src_dir}" -B "${_build_dir}" -DGENTEST_EXPLICIT_MOCK_COMMAND_PROBE=ON
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target explicit_late_link_consumer
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
gentest_check_run_or_fail(
  COMMAND "${_build_dir}/explicit_late_link_consumer${CMAKE_EXECUTABLE_SUFFIX}"
    "--run=explicit_mock_target/compile_command_change"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
