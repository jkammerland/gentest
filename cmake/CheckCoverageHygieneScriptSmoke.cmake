if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCoverageHygieneScriptSmoke.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckCoverageHygieneScriptSmoke.cmake: BUILD_ROOT not set")
endif()

include("${SOURCE_DIR}/cmake/CheckFixtureWriteHelpers.cmake")

set(_work_dir "${BUILD_ROOT}/coverage_hygiene_smoke")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_src_dir "${_work_dir}/src")
set(_clang_build_dir "${_work_dir}/build-clang")
set(_gcc_build_dir "${_work_dir}/build-gcc")
set(_fake_tool_dir "${_work_dir}/fake_tools")
set(_log_dir "${_work_dir}/logs")
set(_driver_py "${_fake_tool_dir}/fake_gcov_frontend.py")
set(_source_file "${_src_dir}/demo.cpp")
set(_clang_log "${_log_dir}/llvm-cov.log")
set(_gcc_log "${_log_dir}/gcov.log")
set(_space_dir "${_work_dir}/tool with spaces")
set(_spaced_log "${_log_dir}/llvm-cov-spaced.log")
set(_single_token_driver_py "${_fake_tool_dir}/fake_single_token_gcov.py")
set(_single_token_log "${_log_dir}/single-token.log")
file(MAKE_DIRECTORY "${_src_dir}" "${_clang_build_dir}/obj" "${_gcc_build_dir}/obj" "${_fake_tool_dir}" "${_log_dir}")

gentest_fixture_write_file(
  "${_source_file}"
  "int demo() {\n"
  "    return 42;\n"
  "}\n")

gentest_fixture_write_file(
  "${_driver_py}"
  "import pathlib\n"
  "import sys\n"
  "\n"
  "mode = sys.argv[1]\n"
  "log_path = pathlib.Path(sys.argv[2])\n"
  "source = sys.argv[3]\n"
  "args = sys.argv[4:]\n"
  "log_path.parent.mkdir(parents=True, exist_ok=True)\n"
  "with log_path.open('a', encoding='utf-8') as fh:\n"
  "    fh.write('MODE=' + mode + '\\n')\n"
  "    fh.write('ARGS=' + ' '.join(args) + '\\n')\n"
  "if mode == 'llvm-cov' and args and args[0] == 'gcov':\n"
  "    args = args[1:]\n"
  "if '--help' in args:\n"
  "    print('  --json-format  --preserve-paths  -j')\n"
  "    raise SystemExit(0)\n"
  "print(f\"File '{source}'\")\n"
  "print('Lines executed:50.00% of 2')\n")

gentest_fixture_write_file(
  "${_single_token_driver_py}"
  "import pathlib\n"
  "import sys\n"
  "\n"
  "log_path = pathlib.Path(r'${_single_token_log}')\n"
  "log_path.parent.mkdir(parents=True, exist_ok=True)\n"
  "with log_path.open('a', encoding='utf-8') as fh:\n"
  "    fh.write('ARGS=' + ' '.join(sys.argv[1:]) + '\\n')\n"
  "if '--help' in sys.argv[1:]:\n"
  "    print('  --json-format  --preserve-paths  -j')\n"
  "    raise SystemExit(0)\n"
  "print(\"File '${_source_file}'\")\n"
  "print('Lines executed:50.00% of 2')\n")

find_program(_python3_program NAMES python3 python)
if(NOT _python3_program)
  message(FATAL_ERROR "coverage_hygiene.py smoke requires python3 or python in PATH")
endif()

if(WIN32)
  set(_path_sep ";")
  set(_fake_llvm_cov "${_fake_tool_dir}/llvm-cov.bat")
  set(_fake_gcov "${_fake_tool_dir}/gcov.bat")
  set(_spaced_llvm_cov "${_space_dir}/llvm-cov.bat")
  gentest_fixture_write_file(
    "${_fake_llvm_cov}"
    "@echo off\r\n"
    "\"${_python3_program}\" \"${_driver_py}\" llvm-cov \"${_clang_log}\" \"${_source_file}\" %*\r\n"
    "exit /B %ERRORLEVEL%\r\n")
  gentest_fixture_write_file(
    "${_fake_gcov}"
    "@echo off\r\n"
    "\"${_python3_program}\" \"${_driver_py}\" gcov \"${_gcc_log}\" \"${_source_file}\" %*\r\n"
    "exit /B %ERRORLEVEL%\r\n")
  file(MAKE_DIRECTORY "${_space_dir}")
  gentest_fixture_write_file(
    "${_spaced_llvm_cov}"
    "@echo off\r\n"
    "\"${_python3_program}\" \"${_driver_py}\" llvm-cov \"${_spaced_log}\" \"${_source_file}\" %*\r\n"
    "exit /B %ERRORLEVEL%\r\n")
else()
  set(_path_sep ":")
  set(_fake_llvm_cov "${_fake_tool_dir}/llvm-cov")
  set(_fake_gcov "${_fake_tool_dir}/gcov")
  set(_spaced_llvm_cov "${_space_dir}/llvm-cov")
  gentest_fixture_write_file(
    "${_fake_llvm_cov}"
    "#!/bin/sh\n"
    "exec \"${_python3_program}\" \"${_driver_py}\" llvm-cov \"${_clang_log}\" \"${_source_file}\" \"$@\"\n")
  gentest_fixture_write_file(
    "${_fake_gcov}"
    "#!/bin/sh\n"
    "exec \"${_python3_program}\" \"${_driver_py}\" gcov \"${_gcc_log}\" \"${_source_file}\" \"$@\"\n")
  file(MAKE_DIRECTORY "${_space_dir}")
  gentest_fixture_write_file(
    "${_spaced_llvm_cov}"
    "#!/bin/sh\n"
    "exec \"${_python3_program}\" \"${_driver_py}\" llvm-cov \"${_spaced_log}\" \"${_source_file}\" \"$@\"\n")
  file(CHMOD "${_fake_llvm_cov}" "${_fake_gcov}" "${_spaced_llvm_cov}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()

gentest_fixture_make_compdb_entry(
  _clang_entry
  DIRECTORY "${_clang_build_dir}"
  FILE "${_source_file}"
  ARGUMENTS
    clang++
    -c "${_source_file}"
    -o "${_clang_build_dir}/obj/demo.cpp.o")
gentest_fixture_make_compdb_entry(
  _gcc_entry
  DIRECTORY "${_gcc_build_dir}"
  FILE "${_source_file}"
  ARGUMENTS
    g++
    -c "${_source_file}"
    -o "${_gcc_build_dir}/obj/demo.cpp.o")
gentest_fixture_write_compdb("${_clang_build_dir}/compile_commands.json" "${_clang_entry}")
gentest_fixture_write_compdb("${_gcc_build_dir}/compile_commands.json" "${_gcc_entry}")

gentest_fixture_write_file(
  "${_clang_build_dir}/CMakeCache.txt"
  "CMAKE_CXX_COMPILER_ID:STRING=Clang\n"
  "CMAKE_CXX_COMPILER:FILEPATH=/usr/bin/clang++\n")
gentest_fixture_write_file(
  "${_gcc_build_dir}/CMakeCache.txt"
  "CMAKE_CXX_COMPILER_ID:STRING=GNU\n"
  "CMAKE_CXX_COMPILER:FILEPATH=/usr/bin/g++\n")

gentest_fixture_write_file("${_clang_build_dir}/obj/demo.cpp.gcda" "clang\n")
gentest_fixture_write_file("${_gcc_build_dir}/obj/demo.cpp.gcda" "gcc\n")

set(_path_env "${_fake_tool_dir}${_path_sep}$ENV{PATH}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=${_path_env}"
    "${_python3_program}" "${SOURCE_DIR}/scripts/coverage_hygiene.py"
      --build-dir "${_clang_build_dir}"
      --roots "${_src_dir}"
      --ignore-statuses stamp_mismatch
  RESULT_VARIABLE _clang_rc
  OUTPUT_VARIABLE _clang_out
  ERROR_VARIABLE _clang_err)
if(NOT _clang_rc EQUAL 0)
  message(FATAL_ERROR
    "coverage_hygiene.py smoke failed for the Clang auto-selection path.\n"
    "stdout:\n${_clang_out}\n"
    "stderr:\n${_clang_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=${_path_env}"
    "${_python3_program}" "${SOURCE_DIR}/scripts/coverage_hygiene.py"
      --build-dir "${_gcc_build_dir}"
      --roots "${_src_dir}"
      --ignore-statuses stamp_mismatch
  RESULT_VARIABLE _gcc_rc
  OUTPUT_VARIABLE _gcc_out
  ERROR_VARIABLE _gcc_err)
if(NOT _gcc_rc EQUAL 0)
  message(FATAL_ERROR
    "coverage_hygiene.py smoke failed for the GCC auto-selection path.\n"
    "stdout:\n${_gcc_out}\n"
    "stderr:\n${_gcc_err}")
endif()

if(NOT EXISTS "${_clang_log}")
  message(FATAL_ERROR "Clang coverage_hygiene smoke never invoked the fake llvm-cov frontend.")
endif()
if(NOT EXISTS "${_gcc_log}")
  message(FATAL_ERROR "GCC coverage_hygiene smoke never invoked the fake gcov frontend.")
endif()

file(READ "${_clang_log}" _clang_log_text)
file(READ "${_gcc_log}" _gcc_log_text)
if(NOT _clang_log_text MATCHES "MODE=llvm-cov")
  message(FATAL_ERROR
    "coverage_hygiene.py did not auto-select llvm-cov gcov for the Clang build directory.\n"
    "Log:\n${_clang_log_text}")
endif()
if(NOT _gcc_log_text MATCHES "MODE=gcov")
  message(FATAL_ERROR
    "coverage_hygiene.py did not auto-select gcov for the GCC build directory.\n"
    "Log:\n${_gcc_log_text}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=${_path_env}"
    "${_python3_program}" "${SOURCE_DIR}/scripts/coverage_hygiene.py"
      --build-dir "${_clang_build_dir}"
      --roots "${_src_dir}"
      --ignore-statuses stamp_mismatch
      --gcov "${_spaced_llvm_cov}" gcov
  RESULT_VARIABLE _explicit_rc
  OUTPUT_VARIABLE _explicit_out
  ERROR_VARIABLE _explicit_err)
if(NOT _explicit_rc EQUAL 0)
  message(FATAL_ERROR
    "coverage_hygiene.py smoke failed for an explicit --gcov override whose frontend path contains spaces.\n"
    "stdout:\n${_explicit_out}\n"
    "stderr:\n${_explicit_err}")
endif()
if(NOT EXISTS "${_spaced_log}")
  message(FATAL_ERROR
    "coverage_hygiene.py explicit --gcov smoke never invoked the spaced frontend path.")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=${_path_env}"
    "${_python3_program}" "${SOURCE_DIR}/scripts/coverage_hygiene.py"
      --build-dir "${_clang_build_dir}"
      --roots "${_src_dir}"
      --ignore-statuses stamp_mismatch
      --gcov "${_python3_program} ${_single_token_driver_py}"
  RESULT_VARIABLE _single_token_rc
  OUTPUT_VARIABLE _single_token_out
  ERROR_VARIABLE _single_token_err)
if(NOT _single_token_rc EQUAL 0)
  message(FATAL_ERROR
    "coverage_hygiene.py smoke failed for the single-token quoted --gcov command form.\n"
    "stdout:\n${_single_token_out}\n"
    "stderr:\n${_single_token_err}")
endif()
if(NOT EXISTS "${_single_token_log}")
  message(FATAL_ERROR
    "coverage_hygiene.py single-token --gcov smoke never invoked the explicit command frontend.")
endif()
