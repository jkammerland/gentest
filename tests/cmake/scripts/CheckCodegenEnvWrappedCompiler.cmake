if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckCodegenEnvWrappedCompiler.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckCodegenEnvWrappedCompiler.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCodegenEnvWrappedCompiler.cmake: SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

find_program(_real_cxx NAMES c++ g++ clang++ clang++-21 clang++-20 REQUIRED)
file(TO_CMAKE_PATH "${_real_cxx}" _real_cxx_norm)
file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)
find_program(_env_program NAMES env)
if(_env_program)
  file(TO_CMAKE_PATH "${_env_program}" _env_program_norm)
endif()

set(_work_dir "${BUILD_ROOT}/codegen_env_wrapped_compiler")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(TO_CMAKE_PATH "${_work_dir}" _work_dir_norm)

set(_sample_cmake_env_cpp "${_work_dir}/sample_cmake_env.cpp")
set(_sample_cmake_env_obj "${_work_dir}/sample_cmake_env.o")
set(_sample_plain_env_cpp "${_work_dir}/sample_plain_env.cpp")
set(_sample_plain_env_obj "${_work_dir}/sample_plain_env.o")

file(COPY
  "${SOURCE_DIR}/tests/cmake/codegen_env_wrapped_compiler/sample_cmake_env.cpp"
  "${SOURCE_DIR}/tests/cmake/codegen_env_wrapped_compiler/sample_plain_env.cpp"
  DESTINATION "${_work_dir}")

file(TO_CMAKE_PATH "${_sample_cmake_env_cpp}" _sample_cmake_env_cpp_norm)
file(TO_CMAKE_PATH "${_sample_cmake_env_obj}" _sample_cmake_env_obj_norm)
file(TO_CMAKE_PATH "${_sample_plain_env_cpp}" _sample_plain_env_cpp_norm)
file(TO_CMAKE_PATH "${_sample_plain_env_obj}" _sample_plain_env_obj_norm)
gentest_make_public_api_include_args(_public_include_args SOURCE_ROOT "${_source_dir_norm}" APPLE_SYSROOT)

gentest_fixture_make_compdb_entry(_cmake_env_entry
  DIRECTORY "${_work_dir_norm}"
  FILE "${_sample_cmake_env_cpp_norm}"
  ARGUMENTS
    "${CMAKE_COMMAND}" "-E" "env" "FOO=1"
    "${_real_cxx_norm}" "-std=c++20"
    ${_public_include_args} "-I${_work_dir_norm}"
    "-c" "${_sample_cmake_env_cpp_norm}" "-o" "${_sample_cmake_env_obj_norm}")
set(_compdb_entries "${_cmake_env_entry}")
if(UNIX AND NOT CMAKE_HOST_WIN32 AND _env_program)
  gentest_fixture_make_compdb_entry(_plain_env_entry
    DIRECTORY "${_work_dir_norm}"
    FILE "${_sample_plain_env_cpp_norm}"
    ARGUMENTS
      "${_env_program_norm}" "FOO=1"
      "${_real_cxx_norm}" "-std=c++20"
      ${_public_include_args} "-I${_work_dir_norm}"
      "-c" "${_sample_plain_env_cpp_norm}" "-o" "${_sample_plain_env_obj_norm}")
  list(APPEND _compdb_entries "${_plain_env_entry}")
endif()
gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" ${_compdb_entries})

execute_process(
  COMMAND "${PROG}" --check --compdb "${_work_dir}" "${_sample_cmake_env_cpp_norm}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR
    "gentest_codegen failed for an env-wrapped non-clang compile command.\n"
    "Output:\n${_out}\nErrors:\n${_err}")
endif()

if(UNIX AND NOT CMAKE_HOST_WIN32 AND _env_program)
  execute_process(
    COMMAND "${PROG}" --check --compdb "${_work_dir}" "${_sample_plain_env_cpp_norm}"
    RESULT_VARIABLE _plain_rc
    OUTPUT_VARIABLE _plain_out
    ERROR_VARIABLE _plain_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if(NOT _plain_rc EQUAL 0)
    message(FATAL_ERROR
      "gentest_codegen failed for a plain env-wrapped compile command.\n"
      "Output:\n${_plain_out}\nErrors:\n${_plain_err}")
  endif()
endif()
