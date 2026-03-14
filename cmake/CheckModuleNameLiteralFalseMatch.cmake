# Requires:
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DBUILD_ROOT=<path to parent build dir>

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleNameLiteralFalseMatch.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleNameLiteralFalseMatch.cmake: BUILD_ROOT not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_work_dir "${BUILD_ROOT}/module_name_literal_false_match")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(WRITE "${_work_dir}/present_header.hpp" "#pragma once\n")
file(MAKE_DIRECTORY "${_work_dir}/inc")
file(WRITE "${_work_dir}/inc/present_dir_header.hpp" "#pragma once\n")

set(_source "${_work_dir}/literal_false_match.cppm")
file(WRITE "${_source}"
  "#define BANNER \\\n"
  "  export module wrong.macro;\n"
  "#define OFF_MACRO 0\n"
  "#if 0\n"
  "export module wrong.inactive;\n"
  "#endif\n"
  "#if defined(OFF_MACRO) && OFF_MACRO >= 2\n"
  "export module wrong.conditional;\n"
  "#endif\n"
  "#if __has_include(\"definitely_missing_header.hpp\")\n"
  "export module wrong.has_include;\n"
  "#endif\n"
  "const char *banner = \"export module wrong.literal;\";\n"
  "export module real.module;\n")

set(_shift_source "${_work_dir}/shift_expression.cppm")
file(WRITE "${_shift_source}"
  "#if (1u << 2) == 4u\n"
  "export module shift.module;\n"
  "#endif\n")

set(_has_include_source "${_work_dir}/has_include_true.cppm")
file(WRITE "${_has_include_source}"
  "#if __has_include(\"present_header.hpp\")\n"
  "export module include.module;\n"
  "#endif\n")

set(_macro_has_include_source "${_work_dir}/has_include_macro.cppm")
file(WRITE "${_macro_has_include_source}"
  "#define HDR \"present_header.hpp\"\n"
  "#if __has_include(HDR)\n"
  "export module macro.include;\n"
  "#endif\n")

set(_octal_source "${_work_dir}/octal_expression.cppm")
file(WRITE "${_octal_source}"
  "#if 010u == 8u\n"
  "export module octal.module;\n"
  "#endif\n")

set(_binary_source "${_work_dir}/binary_expression.cppm")
file(WRITE "${_binary_source}"
  "#if 0b10u == 2u\n"
  "export module binary.module;\n"
  "#endif\n")

set(_digit_sep_source "${_work_dir}/digit_separated_expression.cppm")
file(WRITE "${_digit_sep_source}"
  "#if 1'0u == 10u\n"
  "export module digitsep.module;\n"
  "#endif\n")

set(_include_dir_source "${_work_dir}/has_include_include_dir.cppm")
file(WRITE "${_include_dir_source}"
  "#if __has_include(\"present_dir_header.hpp\")\n"
  "export module include_dir.module;\n"
  "#endif\n")

include("${GENTEST_SOURCE_DIR}/cmake/GentestCodegen.cmake")

_gentest_extract_module_name("${_source}" _module_name)

if(NOT _module_name STREQUAL "real.module")
  message(FATAL_ERROR
    "Expected module name 'real.module', got '${_module_name}'")
endif()

_gentest_extract_module_name("${_shift_source}" _shift_module_name)
if(NOT _shift_module_name STREQUAL "shift.module")
  message(FATAL_ERROR
    "Expected module name 'shift.module', got '${_shift_module_name}'")
endif()

_gentest_extract_module_name("${_has_include_source}" _has_include_module_name)
if(NOT _has_include_module_name STREQUAL "include.module")
  message(FATAL_ERROR
    "Expected module name 'include.module', got '${_has_include_module_name}'")
endif()

_gentest_extract_module_name("${_macro_has_include_source}" _macro_has_include_module_name)
if(NOT _macro_has_include_module_name STREQUAL "macro.include")
  message(FATAL_ERROR
    "Expected module name 'macro.include', got '${_macro_has_include_module_name}'")
endif()

_gentest_extract_module_name("${_octal_source}" _octal_module_name)
if(NOT _octal_module_name STREQUAL "octal.module")
  message(FATAL_ERROR
    "Expected module name 'octal.module', got '${_octal_module_name}'")
endif()

_gentest_extract_module_name("${_binary_source}" _binary_module_name)
if(NOT _binary_module_name STREQUAL "binary.module")
  message(FATAL_ERROR
    "Expected module name 'binary.module', got '${_binary_module_name}'")
endif()

_gentest_extract_module_name("${_digit_sep_source}" _digit_sep_module_name)
if(NOT _digit_sep_module_name STREQUAL "digitsep.module")
  message(FATAL_ERROR
    "Expected module name 'digitsep.module', got '${_digit_sep_module_name}'")
endif()

_gentest_extract_module_name("${_include_dir_source}" _include_dir_module_name "${_work_dir}/inc")
if(NOT _include_dir_module_name STREQUAL "include_dir.module")
  message(FATAL_ERROR
    "Expected module name 'include_dir.module', got '${_include_dir_module_name}'")
endif()

if(DEFINED PROG AND NOT "${PROG}" STREQUAL "")
  gentest_resolve_clang_fixture_compilers(_clang _clangxx)
  if(NOT "${_clangxx}" STREQUAL "")
    set(_cxx "${_clangxx}")
  elseif(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
    set(_cxx "${CXX_COMPILER}")
  else()
    find_program(_cxx NAMES clang++ clang++-22 clang++-21 clang++-20)
  endif()
  if(NOT _cxx)
    gentest_skip_test("module name false-match regression: no clang++ compiler available")
    return()
  endif()
  execute_process(
    COMMAND "${_cxx}" -print-prog-name=clang++
    RESULT_VARIABLE _cxx_print_rc
    OUTPUT_VARIABLE _cxx_print
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(_cxx_print_rc EQUAL 0 AND IS_ABSOLUTE "${_cxx_print}" AND EXISTS "${_cxx_print}")
    set(_cxx "${_cxx_print}")
  endif()

  set(_codegen_source "${_work_dir}/conditional_false_match.ixx")
  file(WRITE "${_codegen_source}"
    "module;\n"
    "#include \"gentest/attributes.h\"\n"
    "\n"
    "#define HDR \"present_dir_header.hpp\"\n"
    "#define OFF_MACRO 0\n"
    "#if defined(OFF_MACRO) && OFF_MACRO >= 2\n"
    "export module wrong_conditional;\n"
    "#endif\n"
    "#if !__has_include(HDR)\n"
    "export module wrong_has_include;\n"
    "#endif\n"
    "#if 0b10u == 2u && 010u == 8u && __has_include(HDR)\n"
    "export module real_conditional;\n"
    "#endif\n"
    "\n"
    "export namespace conditional_false_match {\n"
    "[[using gentest: test(\"conditional/selected_module\")]]\n"
    "void conditional_selected_module() {}\n"
    "}\n")

  gentest_resolve_clang_fixture_compilers(_clang _clangxx)
  if("${_clang}" STREQUAL "" OR "${_clangxx}" STREQUAL "")
    gentest_skip_test("module name false-match regression: clang/clang++ fixture compilers not found")
    return()
  endif()

  set(_fixture_src_dir "${_work_dir}/fixture_src")
  set(_fixture_build_dir "${_work_dir}/fixture_build")
  set(_generated_dir "${_fixture_build_dir}/gentest_codegen")
  file(MAKE_DIRECTORY "${_fixture_src_dir}")
  file(WRITE "${_fixture_src_dir}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.31)\n"
    "project(module_name_false_match_fixture LANGUAGES CXX)\n"
    "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
    "set(CMAKE_CXX_EXTENSIONS OFF)\n"
    "set(gentest_BUILD_TESTING OFF CACHE BOOL \"\" FORCE)\n"
    "set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL \"\" FORCE)\n"
    "add_subdirectory(\"${GENTEST_SOURCE_DIR}\" gentest-build EXCLUDE_FROM_ALL)\n"
    "add_executable(module_name_false_match_tests)\n"
    "target_compile_features(module_name_false_match_tests PRIVATE cxx_std_20)\n"
    "target_include_directories(module_name_false_match_tests PRIVATE \"\${CMAKE_CURRENT_SOURCE_DIR}/inc\")\n"
    "target_link_libraries(module_name_false_match_tests PRIVATE gentest::gentest gentest::gentest_main)\n"
    "target_sources(module_name_false_match_tests PRIVATE FILE_SET module_cases TYPE CXX_MODULES FILES\n"
    "  \"\${CMAKE_CURRENT_SOURCE_DIR}/conditional_false_match.ixx\")\n"
    "gentest_attach_codegen(module_name_false_match_tests OUTPUT_DIR \"\${CMAKE_CURRENT_BINARY_DIR}/gentest_codegen\")\n")
  file(COPY "${_codegen_source}" DESTINATION "${_fixture_src_dir}")
  file(MAKE_DIRECTORY "${_fixture_src_dir}/inc")
  file(COPY "${_work_dir}/inc/present_dir_header.hpp" DESTINATION "${_fixture_src_dir}/inc")

  set(_cmake_gen_args -G "${GENERATOR}")
  if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
    list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
  endif()
  if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
    list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
  endif()

  set(_fixture_cache_args
    "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}"
    "-DCMAKE_C_COMPILER=${_clang}"
    "-DCMAKE_CXX_COMPILER=${_clangxx}"
    "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}")
  if(GENERATOR STREQUAL "Ninja" OR GENERATOR STREQUAL "Ninja Multi-Config")
    gentest_find_supported_ninja(_supported_ninja _supported_ninja_reason)
    if(NOT _supported_ninja)
      gentest_skip_test("module name false-match regression: ${_supported_ninja_reason}")
      return()
    endif()
    list(APPEND _fixture_cache_args "-DCMAKE_MAKE_PROGRAM=${_supported_ninja}")
  elseif(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
    list(APPEND _fixture_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
  endif()
  if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
    list(APPEND _fixture_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
  endif()
  if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
    list(APPEND _fixture_cache_args "-DLLVM_DIR=${LLVM_DIR}")
  endif()
  if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
    list(APPEND _fixture_cache_args "-DClang_DIR=${Clang_DIR}")
  endif()
  if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
    list(APPEND _fixture_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
  endif()
  gentest_find_clang_scan_deps(_clang_scan_deps "${_clangxx}")
  if(NOT "${_clang_scan_deps}" STREQUAL "")
    list(APPEND _fixture_cache_args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_clang_scan_deps}")
  endif()

  gentest_check_run_or_fail(
    COMMAND
      "${CMAKE_COMMAND}"
      ${_cmake_gen_args}
      -S "${_fixture_src_dir}"
      -B "${_fixture_build_dir}"
      ${_fixture_cache_args}
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)

  gentest_check_run_or_fail(
    COMMAND "${CMAKE_COMMAND}" --build "${_fixture_build_dir}" --target module_name_false_match_tests
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)

  set(_generated_wrapper "${_generated_dir}/tu_0000_conditional_false_match.module.gentest.ixx")
  if(NOT EXISTS "${_generated_wrapper}")
    message(FATAL_ERROR
      "Expected generated module wrapper '${_generated_wrapper}' to exist after building the fixture.")
  endif()

  file(READ "${_generated_wrapper}" _generated_wrapper_text)
  if(NOT _generated_wrapper_text MATCHES "(^|\n)(export[ \t]+)?module[ \t]+real_conditional;")
    message(FATAL_ERROR
      "gentest_codegen did not emit the expected active module declaration.\nGenerated wrapper:\n${_generated_wrapper_text}")
  endif()

  set(_generated_shim "${_generated_dir}/tu_0000_conditional_false_match.gentest.cpp")
  if(NOT EXISTS "${_generated_shim}")
    message(FATAL_ERROR
      "Expected generated shim '${_generated_shim}' to exist after building the fixture.")
  endif()

  file(READ "${_generated_shim}" _generated_shim_text)
  if(NOT _generated_shim_text MATCHES "(^|\n)import[ \t]+real_conditional;")
    message(FATAL_ERROR
      "gentest_codegen did not retarget the registration shim to the expected live module.\nGenerated shim:\n${_generated_shim_text}")
  endif()
  if(_generated_shim_text MATCHES "wrong_conditional" OR _generated_shim_text MATCHES "wrong_has_include")
    message(FATAL_ERROR
      "gentest_codegen retargeted the registration shim to an inactive module.\nGenerated shim:\n${_generated_shim_text}")
  endif()

  set(_fixture_exe "${_fixture_build_dir}/module_name_false_match_tests")
  if(CMAKE_HOST_WIN32)
    set(_fixture_exe "${_fixture_exe}.exe")
  endif()
  gentest_check_run_or_fail(
    COMMAND "${_fixture_exe}" --run=conditional/selected_module
    WORKING_DIRECTORY "${_fixture_build_dir}"
    STRIP_TRAILING_WHITESPACE)
endif()

message(STATUS "Module-name literal false-match regression passed")
