if(WIN32)
  message(STATUS "CheckCodegenResourceDirFromCompdbCompiler.cmake: Windows host; skipping")
  return()
endif()

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckCodegenResourceDirFromCompdbCompiler.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckCodegenResourceDirFromCompdbCompiler.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCodegenResourceDirFromCompdbCompiler.cmake: SOURCE_DIR not set")
endif()

find_program(_real_clang NAMES clang++-21 clang++-20 clang++ REQUIRED)
file(TO_CMAKE_PATH "${_real_clang}" _real_clang_norm)
file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)

set(_work_dir "${BUILD_ROOT}/codegen_resource_dir_from_compdb")
set(_bin_dir "${_work_dir}/bin")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_bin_dir}")

set(_fake_script
  "#!/bin/sh\n"
  "if [ \"$1\" = \"-print-resource-dir\" ]; then\n"
  "  printf '%s\\n' '/definitely/missing/gentest-resource-dir'\n"
  "  exit 0\n"
  "fi\n"
  "exec \"${_real_clang_norm}\" \"$@\"\n")

foreach(_name IN ITEMS clang++ clang++-20 clang++-21 clang++-22)
  file(WRITE "${_bin_dir}/${_name}" "${_fake_script}")
  file(CHMOD "${_bin_dir}/${_name}"
    PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE
      GROUP_READ GROUP_EXECUTE
      WORLD_READ WORLD_EXECUTE)
endforeach()

set(_input_cpp "${_work_dir}/resource_dir_input.cpp")
file(WRITE "${_input_cpp}"
  "#include <stddef.h>\n"
  "#include \"gentest/attributes.h\"\n"
  "[[using gentest: test(\"resource_dir/smoke\")]] void smoke() {}\n")

file(TO_CMAKE_PATH "${_input_cpp}" _input_cpp_norm)
file(WRITE "${_work_dir}/compile_commands.json"
  "[\n"
  "  {\n"
  "    \"directory\": \"${_work_dir}\",\n"
  "    \"file\": \"${_input_cpp_norm}\",\n"
  "    \"command\": \"${_real_clang_norm} -std=c++20 -I${_source_dir_norm}/include -c ${_input_cpp_norm}\"\n"
  "  }\n"
  "]\n")

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env
    "PATH=${_bin_dir}:$ENV{PATH}"
    "${PROG}"
    --check
    --compdb "${_work_dir}"
    "${_input_cpp_norm}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR
    "gentest_codegen should use the compilation-database compiler for -resource-dir probing. Output:\n${_out}\nErrors:\n${_err}")
endif()
