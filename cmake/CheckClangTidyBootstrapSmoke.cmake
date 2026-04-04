if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckClangTidyBootstrapSmoke.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckClangTidyBootstrapSmoke.cmake: BUILD_ROOT not set")
endif()

if(WIN32)
  message("GENTEST_SKIP_TEST: clang-tidy bootstrap smoke uses POSIX shell wrappers")
  return()
endif()

find_program(_bash_program NAMES bash)
if(NOT _bash_program)
  message("GENTEST_SKIP_TEST: bash is required for the clang-tidy bootstrap smoke")
  return()
endif()

include("${SOURCE_DIR}/cmake/CheckFixtureWriteHelpers.cmake")

set(_work_dir "${BUILD_ROOT}/clang_tidy_bootstrap_smoke")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(CREATE_LINK "${SOURCE_DIR}/tests" "${_work_dir}/tests" SYMBOLIC)

set(_build_dir "${_work_dir}/build")
set(_generated_dir "${_build_dir}/tests/generated/mock_surface")
set(_generated_public_dir "${_generated_dir}/public")
set(_command_build_dir "${_work_dir}/build_command")
set(_command_generated_dir "${_command_build_dir}/tests/generated/mock surface")
set(_command_generated_public_dir "${_command_generated_dir}/public")
set(_fake_bin_dir "${_work_dir}/fake_bin")
set(_cmake_log "${_work_dir}/cmake_build.log")
set(_command_cmake_log "${_work_dir}/cmake_build_command.log")
set(_rewritten_compdb_log "${_work_dir}/rewritten_compile_commands.json")
set(_command_rewritten_compdb_log "${_work_dir}/rewritten_compile_commands_command.json")
set(_fake_cmake "${_fake_bin_dir}/cmake")
set(_fake_tidy "${_fake_bin_dir}/fake-clang-tidy")
file(MAKE_DIRECTORY
  "${_build_dir}"
  "${_generated_public_dir}"
  "${_command_build_dir}"
  "${_command_generated_public_dir}"
  "${_fake_bin_dir}")

gentest_fixture_write_file(
  "${_fake_cmake}"
  "#!/bin/sh\n"
  "log_file='${_cmake_log}'\n"
  "for arg in \"$@\"; do\n"
  "  if [ \"$arg\" = '${_command_build_dir}' ]; then\n"
  "    log_file='${_command_cmake_log}'\n"
  "    break\n"
  "  fi\n"
  "done\n"
  "printf '%s\\n' \"$@\" >> \"$log_file\"\n"
  "exit 0\n")
gentest_fixture_write_file(
  "${_fake_tidy}"
  "#!/bin/sh\n"
  "while [ \"$#\" -gt 0 ]; do\n"
  "  if [ \"$1\" = '-p' ] && [ \"$#\" -ge 2 ]; then\n"
  "    case \"$2\" in\n"
  "      *build_command*) cp \"$2/compile_commands.json\" '${_command_rewritten_compdb_log}' ;;\n"
  "      *) cp \"$2/compile_commands.json\" '${_rewritten_compdb_log}' ;;\n"
  "    esac\n"
  "    shift 2\n"
  "    continue\n"
  "  fi\n"
  "  shift\n"
  "done\n"
  "exit 0\n")
file(CHMOD "${_fake_cmake}" "${_fake_tidy}"
  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

set(_generated_source "${_generated_dir}/tu_0000_cases.gentest.cpp")
set(_consumer_source "${SOURCE_DIR}/tests/failing/cases.cpp")
gentest_fixture_write_file("${_generated_source}" "#include \"../../../../tests/failing/cases.cpp\"\n")
gentest_fixture_write_file("${_generated_public_dir}/gentest_textual_suite_mocks.hpp" "// smoke header\n")

gentest_fixture_make_compdb_entry(
  _generated_entry
  DIRECTORY "${_build_dir}"
  FILE "${_generated_source}"
  ARGUMENTS
    clang++
    -I"${_generated_public_dir}"
    -c "${_generated_source}"
    -o "${_build_dir}/tests/CMakeFiles/gentest_textual_suite_mocks.dir/generated/mock_surface/tu_0000_cases.gentest.cpp.o")

gentest_fixture_make_compdb_entry(
  _consumer_entry
  DIRECTORY "${_build_dir}"
  FILE "${_consumer_source}"
  ARGUMENTS
    clang++
    -I"${_generated_public_dir}"
    -c "${_consumer_source}"
    -o "${_build_dir}/tests/CMakeFiles/gentest_failing_tests.dir/tests/failing/cases.cpp.o")

gentest_fixture_write_compdb("${_build_dir}/compile_commands.json" "${_generated_entry}" "${_consumer_entry}")

set(_command_generated_source "${_command_generated_dir}/tu_0001_cases.gentest.cpp")
gentest_fixture_write_file("${_command_generated_source}" "#include \"../../../../tests/failing/cases.cpp\"\n")
gentest_fixture_write_file("${_command_generated_public_dir}/gentest_textual_suite_mocks.hpp" "// smoke header\n")
set(_windowsish_output "C:\\smoke\\tests\\CMakeFiles\\gentest_textual_suite_mocks.dir\\generated\\mock_surface\\tu_0001_cases.gentest.cpp.obj")
string(REPLACE " " "\\ " _escaped_command_generated_public_dir "${_command_generated_public_dir}")
string(REPLACE " " "\\ " _escaped_command_generated_source "${_command_generated_source}")
gentest_fixture_make_compdb_entry(
  _command_generated_entry
  DIRECTORY "${_command_build_dir}"
  FILE "${_command_generated_source}"
  COMMAND
    "clang++ \"-I${_command_generated_public_dir}\" -c \"${_command_generated_source}\" -o ${_windowsish_output}")
gentest_fixture_make_compdb_entry(
  _command_consumer_entry
  DIRECTORY "${_command_build_dir}"
  FILE "${_consumer_source}"
  COMMAND
    "clang++ \"-I${_command_generated_public_dir}\" -c ${_consumer_source} -o ${_command_build_dir}/tests/CMakeFiles/gentest_failing_tests.dir/tests/failing/cases.cpp.o")
gentest_fixture_write_compdb(
  "${_command_build_dir}/compile_commands.json"
  "${_command_generated_entry}"
  "${_command_consumer_entry}")

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
    "check_clang_tidy.sh bootstrap smoke failed for the arguments-based consumer/include-path path.\n"
    "stdout:\n${_tidy_out}\n"
    "stderr:\n${_tidy_err}")
endif()

if(NOT EXISTS "${_cmake_log}")
  message(FATAL_ERROR "Expected fake cmake build log was not created: ${_cmake_log}")
endif()
file(READ "${_cmake_log}" _cmake_log_text)
string(FIND "${_cmake_log_text}" "--target" _target_flag_pos)
string(FIND "${_cmake_log_text}" "gentest_textual_suite_mocks" _producer_target_pos)
if(_target_flag_pos EQUAL -1 OR _producer_target_pos EQUAL -1)
  message(FATAL_ERROR
    "Bootstrap smoke did not request the generated producer target from an arguments-only compile database.\n"
    "Captured cmake --build args:\n${_cmake_log_text}")
endif()
string(FIND "${_cmake_log_text}" "gentest_failing_tests" _consumer_target_pos)
if(NOT _consumer_target_pos EQUAL -1)
  message(FATAL_ERROR
    "Bootstrap smoke widened to the full consuming target instead of the generated producer target for the arguments-based consumer/include-path path.\n"
    "Captured cmake --build args:\n${_cmake_log_text}")
endif()

file(REMOVE "${_rewritten_compdb_log}")
gentest_fixture_write_compdb("${_build_dir}/compile_commands.json" "${_generated_entry}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=${_fake_bin_dir}:$ENV{PATH}"
    "CLANG_TIDY_BIN=${_fake_tidy}"
    "CLANG_TIDY_JOBS=1"
    "${_bash_program}" "${SOURCE_DIR}/scripts/check_clang_tidy.sh" "${_build_dir}" check
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _remap_tidy_rc
  OUTPUT_VARIABLE _remap_tidy_out
  ERROR_VARIABLE _remap_tidy_err)
if(NOT _remap_tidy_rc EQUAL 0)
  message(FATAL_ERROR
    "check_clang_tidy.sh bootstrap smoke failed for the generated-only remap path.\n"
    "stdout:\n${_remap_tidy_out}\n"
    "stderr:\n${_remap_tidy_err}")
endif()
if(NOT EXISTS "${_rewritten_compdb_log}")
  message(FATAL_ERROR "Expected rewritten compile_commands log was not created: ${_rewritten_compdb_log}")
endif()
file(READ "${_rewritten_compdb_log}" _rewritten_compdb_text)
string(FIND "${_rewritten_compdb_text}" "\"file\": \"${_consumer_source}\"" _rewritten_file_pos)
string(FIND "${_rewritten_compdb_text}" "gentest_textual_suite_mocks.dir" _rewritten_target_fragment_pos)
string(FIND "${_rewritten_compdb_text}" "tu_0000_cases.gentest.cpp.o" _rewritten_object_fragment_pos)
string(FIND "${_rewritten_compdb_text}" "\"${_generated_source}\"" _rewritten_generated_source_pos)
if(_rewritten_file_pos EQUAL -1 OR _rewritten_target_fragment_pos EQUAL -1 OR _rewritten_object_fragment_pos EQUAL -1)
  message(FATAL_ERROR
    "Bootstrap smoke did not remap the generated-only TU back to the repo source in the rewritten compile database.\n"
    "Captured compile_commands:\n${_rewritten_compdb_text}")
endif()
if(NOT _rewritten_generated_source_pos EQUAL -1)
  message(FATAL_ERROR
    "Bootstrap smoke left the original generated-only source path in the rewritten compile database.\n"
    "Captured compile_commands:\n${_rewritten_compdb_text}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=${_fake_bin_dir}:$ENV{PATH}"
    "CLANG_TIDY_BIN=${_fake_tidy}"
    "CLANG_TIDY_JOBS=1"
    "${_bash_program}" "${SOURCE_DIR}/scripts/check_clang_tidy.sh" "${_command_build_dir}" check
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _command_tidy_rc
  OUTPUT_VARIABLE _command_tidy_out
  ERROR_VARIABLE _command_tidy_err)
if(NOT _command_tidy_rc EQUAL 0)
  message(FATAL_ERROR
    "check_clang_tidy.sh bootstrap smoke failed for the command-string parsing path.\n"
    "stdout:\n${_command_tidy_out}\n"
    "stderr:\n${_command_tidy_err}")
endif()

if(NOT EXISTS "${_command_cmake_log}")
  message(FATAL_ERROR "Expected fake cmake build log for the command-string path was not created: ${_command_cmake_log}")
endif()
file(READ "${_command_cmake_log}" _command_cmake_log_text)
string(FIND "${_command_cmake_log_text}" "--target" _command_target_flag_pos)
string(FIND "${_command_cmake_log_text}" "gentest_textual_suite_mocks" _command_producer_target_pos)
if(_command_target_flag_pos EQUAL -1 OR _command_producer_target_pos EQUAL -1)
  message(FATAL_ERROR
    "Bootstrap smoke did not request the generated producer target from a command-only compile database entry.\n"
    "Captured cmake --build args:\n${_command_cmake_log_text}")
endif()
string(FIND "${_command_cmake_log_text}" "gentest_failing_tests" _command_consumer_target_pos)
if(NOT _command_consumer_target_pos EQUAL -1)
  message(FATAL_ERROR
    "Bootstrap smoke widened to the full consuming target instead of the generated producer target for the command-string path.\n"
    "Captured cmake --build args:\n${_command_cmake_log_text}")
endif()

file(REMOVE "${_command_rewritten_compdb_log}")
gentest_fixture_make_compdb_entry(
  _command_generated_escaped_entry
  DIRECTORY "${_command_build_dir}"
  FILE "${_command_generated_source}"
  COMMAND
    "clang++ -I${_escaped_command_generated_public_dir} -c ${_escaped_command_generated_source} -o ${_windowsish_output}")
gentest_fixture_write_compdb("${_command_build_dir}/compile_commands.json" "${_command_generated_escaped_entry}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=${_fake_bin_dir}:$ENV{PATH}"
    "CLANG_TIDY_BIN=${_fake_tidy}"
    "CLANG_TIDY_JOBS=1"
    "${_bash_program}" "${SOURCE_DIR}/scripts/check_clang_tidy.sh" "${_command_build_dir}" check
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _command_remap_tidy_rc
  OUTPUT_VARIABLE _command_remap_tidy_out
  ERROR_VARIABLE _command_remap_tidy_err)
if(NOT _command_remap_tidy_rc EQUAL 0)
  message(FATAL_ERROR
    "check_clang_tidy.sh bootstrap smoke failed for the command-only generated remap path.\n"
    "stdout:\n${_command_remap_tidy_out}\n"
    "stderr:\n${_command_remap_tidy_err}")
endif()
if(NOT EXISTS "${_command_rewritten_compdb_log}")
  message(FATAL_ERROR
    "Expected rewritten compile_commands log for the command-string path was not created: ${_command_rewritten_compdb_log}")
endif()
file(READ "${_command_rewritten_compdb_log}" _command_rewritten_compdb_text)
string(FIND "${_command_rewritten_compdb_text}" "\"file\": \"${_consumer_source}\"" _command_rewritten_file_pos)
string(FIND "${_command_rewritten_compdb_text}" "gentest_textual_suite_mocks.dir" _command_rewritten_target_fragment_pos)
string(FIND "${_command_rewritten_compdb_text}" "tu_0001_cases.gentest.cpp.obj" _command_rewritten_object_fragment_pos)
string(FIND "${_command_rewritten_compdb_text}" "\"${_command_generated_source}\"" _command_rewritten_generated_source_pos)
if(_command_rewritten_file_pos EQUAL -1 OR _command_rewritten_target_fragment_pos EQUAL -1 OR
    _command_rewritten_object_fragment_pos EQUAL -1)
  message(FATAL_ERROR
    "Bootstrap smoke did not remap the command-only generated TU back to the repo source in the rewritten compile database.\n"
    "Captured compile_commands:\n${_command_rewritten_compdb_text}")
endif()
if(NOT _command_rewritten_generated_source_pos EQUAL -1)
  message(FATAL_ERROR
    "Bootstrap smoke left the original command-only generated source path in the rewritten compile database.\n"
    "Captured compile_commands:\n${_command_rewritten_compdb_text}")
endif()
