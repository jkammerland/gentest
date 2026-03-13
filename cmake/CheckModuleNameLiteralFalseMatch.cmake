# Requires:
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DBUILD_ROOT=<path to parent build dir>

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleNameLiteralFalseMatch.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleNameLiteralFalseMatch.cmake: BUILD_ROOT not set")
endif()

set(_work_dir "${BUILD_ROOT}/module_name_literal_false_match")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

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
  "const char *banner = \"export module wrong.literal;\";\n"
  "export module real.module;\n")

include("${GENTEST_SOURCE_DIR}/cmake/GentestCodegen.cmake")

_gentest_extract_module_name("${_source}" _module_name)

if(NOT _module_name STREQUAL "real.module")
  message(FATAL_ERROR
    "Expected module name 'real.module', got '${_module_name}'")
endif()

if(DEFINED PROG AND NOT "${PROG}" STREQUAL "")
  if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
    set(_cxx "${CXX_COMPILER}")
  else()
    find_program(_cxx NAMES clang++ clang++-22 clang++-21 clang++-20 c++)
  endif()
  if(NOT _cxx)
    message(FATAL_ERROR "CheckModuleNameLiteralFalseMatch.cmake: no C++ compiler available for codegen verification")
  endif()

  set(_codegen_source "${_work_dir}/conditional_false_match.cppm")
  file(WRITE "${_codegen_source}"
    "module;\n"
    "#define OFF_MACRO 0\n"
    "#if defined(OFF_MACRO) && OFF_MACRO >= 2\n"
    "export module wrong_conditional;\n"
    "#endif\n"
    "export module real_conditional;\n")

  file(TO_CMAKE_PATH "${_work_dir}" _work_dir_norm)
  file(TO_CMAKE_PATH "${_codegen_source}" _source_norm)
  file(TO_CMAKE_PATH "${_cxx}" _cxx_norm)
  file(WRITE "${_work_dir}/compile_commands.json"
    "[\n"
    "  {\n"
    "    \"directory\": \"${_work_dir_norm}\",\n"
    "    \"file\": \"${_source_norm}\",\n"
    "    \"arguments\": [\"${_cxx_norm}\", \"-std=c++20\", \"-I${GENTEST_SOURCE_DIR}/include\", \"-c\", \"${_source_norm}\", \"-o\", \"${_work_dir_norm}/literal_false_match.o\"]\n"
    "  }\n"
    "]\n")

  execute_process(
    COMMAND "${PROG}" --check --compdb "${_work_dir}" "${_codegen_source}"
    RESULT_VARIABLE _codegen_rc
    OUTPUT_VARIABLE _codegen_out
    ERROR_VARIABLE _codegen_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  if(NOT _codegen_rc EQUAL 0)
    message(FATAL_ERROR
      "gentest_codegen misclassified the inactive conditional module declaration.\n${_codegen_out}\n${_codegen_err}")
  endif()
endif()

message(STATUS "Module-name literal false-match regression passed")
