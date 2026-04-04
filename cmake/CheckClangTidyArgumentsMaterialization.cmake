if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckClangTidyArgumentsMaterialization.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckClangTidyArgumentsMaterialization.cmake: BUILD_ROOT not set")
endif()

if(WIN32)
  message("GENTEST_SKIP_TEST: clang-tidy arguments materialization smoke uses POSIX shell wrappers")
  return()
endif()

find_program(_bash_program NAMES bash)
if(NOT _bash_program)
  message("GENTEST_SKIP_TEST: bash is required for the clang-tidy arguments materialization smoke")
  return()
endif()

include("${SOURCE_DIR}/cmake/CheckFixtureWriteHelpers.cmake")

set(_work_dir "${BUILD_ROOT}/clang_tidy_arguments_materialization")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(CREATE_LINK "${SOURCE_DIR}/tests" "${_work_dir}/tests" SYMBOLIC)

set(_build_dir "${_work_dir}/build")
set(_generated_dir "${_build_dir}/tests/generated/mock_surface")
set(_generated_public_dir "${_generated_dir}/public")
set(_fake_bin_dir "${_work_dir}/fake_bin")
set(_cmake_log "${_work_dir}/cmake_build.log")
set(_tidy_log "${_work_dir}/clang_tidy.log")
set(_fake_cmake "${_fake_bin_dir}/cmake")
set(_fake_tidy "${_fake_bin_dir}/fake-clang-tidy")
file(MAKE_DIRECTORY "${_build_dir}" "${_generated_public_dir}" "${_fake_bin_dir}")

gentest_fixture_write_file(
  "${_fake_cmake}"
  "#!/bin/sh\n"
  "mkdir -p '${_generated_public_dir}'\n"
  "printf '%s\\n' '// materialized by fake cmake' > '${_generated_public_dir}/gentest_textual_suite_mocks.hpp'\n"
  "printf '%s\\n' \"$@\" >> '${_cmake_log}'\n"
  "exit 0\n")
gentest_fixture_write_file(
  "${_fake_tidy}"
  "#!/bin/sh\n"
  "if [ ! -f '${_generated_public_dir}/gentest_textual_suite_mocks.hpp' ]; then\n"
  "  echo 'missing materialized generated surface' >&2\n"
  "  exit 3\n"
  "fi\n"
  "printf '%s\\n' \"$@\" >> '${_tidy_log}'\n"
  "exit 0\n")
file(CHMOD "${_fake_cmake}" "${_fake_tidy}"
  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

set(_generated_source "${_generated_dir}/tu_0000_cases.gentest.cpp")
gentest_fixture_write_file("${_generated_source}" "#include \"../../../../tests/failing/cases.cpp\"\n")

gentest_fixture_make_compdb_entry(
  _generated_entry
  DIRECTORY "${_build_dir}"
  FILE "${_generated_source}"
  ARGUMENTS
    clang++
    -I"${_generated_public_dir}"
    -c "${_generated_source}"
    -o "${_build_dir}/tests/CMakeFiles/gentest_textual_suite_mocks.dir/generated/mock_surface/tu_0000_cases.gentest.cpp.o")
gentest_fixture_write_compdb("${_build_dir}/compile_commands.json" "${_generated_entry}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=${_fake_bin_dir}:$ENV{PATH}"
    "CLANG_TIDY_BIN=${_fake_tidy}"
    "CLANG_TIDY_JOBS=1"
    "${_bash_program}" "${SOURCE_DIR}/scripts/check_clang_tidy.sh" "${_build_dir}" check
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _tidy_rc
  OUTPUT_VARIABLE _tidy_out
  ERROR_VARIABLE _tidy_err)
if(NOT _tidy_rc EQUAL 0)
  message(FATAL_ERROR
    "check_clang_tidy.sh arguments materialization smoke failed.\n"
    "stdout:\n${_tidy_out}\n"
    "stderr:\n${_tidy_err}")
endif()

if(NOT EXISTS "${_cmake_log}")
  message(FATAL_ERROR "Expected fake cmake build log was not created: ${_cmake_log}")
endif()

file(READ "${_cmake_log}" _cmake_log_text)
string(FIND "${_cmake_log_text}" "--target" _target_flag_pos)
string(FIND "${_cmake_log_text}" "gentest_textual_suite_mocks" _generated_target_pos)
if(_target_flag_pos EQUAL -1 OR _generated_target_pos EQUAL -1)
  message(FATAL_ERROR
    "check_clang_tidy.sh did not request the generated target recovered from an arguments-only compile database entry.\n"
    "Captured cmake --build args:\n${_cmake_log_text}\n"
    "stdout:\n${_tidy_out}\n"
    "stderr:\n${_tidy_err}")
endif()

if(NOT EXISTS "${_tidy_log}")
  message(FATAL_ERROR "Expected fake clang-tidy invocation log was not created: ${_tidy_log}")
endif()

file(READ "${_tidy_log}" _tidy_log_text)
string(FIND "${_tidy_log_text}" "${SOURCE_DIR}/tests/failing/cases.cpp" _mapped_repo_source_pos)
if(_mapped_repo_source_pos EQUAL -1)
  message(FATAL_ERROR
    "check_clang_tidy.sh did not invoke clang-tidy on the remapped repo source after materializing the generated target.\n"
    "Captured clang-tidy args:\n${_tidy_log_text}")
endif()
