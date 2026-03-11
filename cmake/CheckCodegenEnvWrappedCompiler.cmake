if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckCodegenEnvWrappedCompiler.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckCodegenEnvWrappedCompiler.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCodegenEnvWrappedCompiler.cmake: SOURCE_DIR not set")
endif()

find_program(_real_cxx NAMES c++ g++ clang++ clang++-21 clang++-20 REQUIRED)
file(TO_CMAKE_PATH "${_real_cxx}" _real_cxx_norm)
file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)

set(_work_dir "${BUILD_ROOT}/codegen_env_wrapped_compiler")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(TO_CMAKE_PATH "${_work_dir}" _work_dir_norm)

set(_sample_cpp "${_work_dir}/sample.cpp")
set(_sample_obj "${_work_dir}/sample.o")

file(WRITE "${_sample_cpp}"
  "#include \"gentest/attributes.h\"\n"
  "[[using gentest: test(\"env/wrapped\")]] void wrapped_test() {}\n")

file(TO_CMAKE_PATH "${_sample_cpp}" _sample_cpp_norm)
file(TO_CMAKE_PATH "${_sample_obj}" _sample_obj_norm)

file(WRITE "${_work_dir}/compile_commands.json"
  "[\n"
  "  {\n"
  "    \"directory\": \"${_work_dir_norm}\",\n"
  "    \"file\": \"${_sample_cpp_norm}\",\n"
  "    \"arguments\": [\"${CMAKE_COMMAND}\", \"-E\", \"env\", \"FOO=1\", \"${_real_cxx_norm}\", \"-std=c++20\", \"-I${_source_dir_norm}/include\", \"-I${_work_dir_norm}\", \"-c\", \"${_sample_cpp_norm}\", \"-o\", \"${_sample_obj_norm}\"]\n"
  "  }\n"
  "]\n")

execute_process(
  COMMAND "${PROG}" --check --compdb "${_work_dir}" "${_sample_cpp_norm}"
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
