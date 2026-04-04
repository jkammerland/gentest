if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCoverageReportScriptSmoke.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckCoverageReportScriptSmoke.cmake: BUILD_ROOT not set")
endif()

include("${SOURCE_DIR}/cmake/CheckFixtureWriteHelpers.cmake")

set(_work_dir "${BUILD_ROOT}/coverage_report_smoke")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_build_dir "${_work_dir}/build")
set(_gcc_build_dir "${_work_dir}/build-gcc")
set(_output_dir "${_build_dir}/out")
set(_fake_module_dir "${_work_dir}/fake_gcovr")
set(_fake_tool_dir "${_work_dir}/fake_tools")
set(_policy_file "${_work_dir}/coverage_policy.toml")
set(_gcovr_log "${_work_dir}/gcovr_args.json")
set(_threshold_output_dir "${_build_dir}/out-threshold")
set(_clang_auto_output_dir "${_build_dir}/out-auto")
set(_gcc_auto_output_dir "${_gcc_build_dir}/out-auto")
set(_gcovr_fail_output_dir "${_build_dir}/out-gcovr-fail")
set(_windows_style_output_dir "${_build_dir}/out-windows-style")
file(MAKE_DIRECTORY "${_build_dir}" "${_gcc_build_dir}" "${_fake_module_dir}" "${_fake_tool_dir}")

gentest_fixture_write_file(
  "${_fake_tool_dir}/fake_gcov.py"
  "import sys\n"
  "raise SystemExit(0)\n")
if(WIN32)
  set(_fake_llvm_cov "${_fake_tool_dir}/llvm-cov.bat")
  set(_fake_gcov "${_fake_tool_dir}/gcov.bat")
  gentest_fixture_write_file(
    "${_fake_llvm_cov}"
    "@echo off\r\n"
    "exit /B 0\r\n")
  gentest_fixture_write_file(
    "${_fake_gcov}"
    "@echo off\r\n"
    "exit /B 0\r\n")
else()
  set(_fake_llvm_cov "${_fake_tool_dir}/llvm-cov")
  set(_fake_gcov "${_fake_tool_dir}/gcov")
  set(_windows_style_llvm_cov "${_fake_tool_dir}/C:\\Tools\\llvm\\bin\\llvm-cov.exe")
  gentest_fixture_write_file(
    "${_fake_llvm_cov}"
    "#!/bin/sh\n"
    "exit 0\n")
  gentest_fixture_write_file(
    "${_fake_gcov}"
    "#!/bin/sh\n"
    "exit 0\n")
  gentest_fixture_write_file(
    "${_windows_style_llvm_cov}"
    "#!/bin/sh\n"
    "exit 0\n")
  file(CHMOD "${_fake_llvm_cov}" "${_fake_gcov}" "${_windows_style_llvm_cov}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()

gentest_fixture_write_file(
  "${_fake_module_dir}/gcovr.py"
  "import os\n"
  "import json\n"
  "import pathlib\n"
  "import shlex\n"
  "import sys\n"
  "\n"
  "args = sys.argv[1:]\n"
  "summary = None\n"
  "html = None\n"
  "payload = {'args': args}\n"
  "for index, arg in enumerate(args):\n"
  "    if arg == '--json-summary' and index + 1 < len(args):\n"
  "        summary = pathlib.Path(args[index + 1])\n"
  "    elif arg == '--html-details' and index + 1 < len(args):\n"
  "        html = pathlib.Path(args[index + 1])\n"
  "    elif arg == '--gcov-executable' and index + 1 < len(args):\n"
  "        payload['gcov_executable_raw'] = args[index + 1]\n"
  "        payload['gcov_executable_parsed'] = shlex.split(args[index + 1])\n"
  "pathlib.Path(r'${_gcovr_log}').write_text(json.dumps(payload), encoding='utf-8')\n"
  "mode = os.environ.get('FAKE_GCOVR_MODE', '')\n"
  "if mode == 'fail':\n"
  "    print('fake gcovr fail')\n"
  "    raise SystemExit(2)\n"
  "if summary is None or html is None:\n"
  "    raise SystemExit('missing required gcovr output arguments')\n"
  "summary.parent.mkdir(parents=True, exist_ok=True)\n"
  "html.parent.mkdir(parents=True, exist_ok=True)\n"
  "summary.write_text(json.dumps({\n"
  "    'line_covered': 8,\n"
  "    'line_total': 10,\n"
  "    'line_percent': 80.0,\n"
  "    'branch_covered': 4,\n"
  "    'branch_total': 10,\n"
  "    'branch_percent': 40.0,\n"
  "    'function_covered': 3,\n"
  "    'function_total': 3,\n"
  "    'function_percent': 100.0,\n"
  "    'files': [\n"
  "        {\n"
  "            'filename': 'src/demo.cpp',\n"
  "            'line_percent': 80.0,\n"
  "            'branch_percent': 40.0,\n"
  "            'function_percent': 100.0,\n"
  "        }\n"
  "    ],\n"
  "}, indent=2), encoding='utf-8')\n"
  "html.write_text('<html><body>fake gcovr</body></html>\\n', encoding='utf-8')\n")

gentest_fixture_write_file(
  "${_policy_file}"
  "roots = [\"src\"]\n"
  "exclude_prefix = []\n"
  "intentional = [\"src/intentional\"]\n"
  "no_exec = []\n"
  "fail_on = \"missing_obj,missing_gcda,zero_hits\"\n"
  "warn_on = \"missing_obj,missing_gcda,zero_hits\"\n"
  "\n"
  "[report]\n"
  "fail_under_line = 75.0\n"
  "fail_under_branch = 35.0\n"
  "top_files = 5\n")

gentest_fixture_write_file(
  "${_build_dir}/CMakeCache.txt"
  "CMAKE_CXX_COMPILER_ID:STRING=Clang\n"
  "CMAKE_CXX_COMPILER:FILEPATH=/usr/bin/clang++\n")
gentest_fixture_write_file(
  "${_gcc_build_dir}/CMakeCache.txt"
  "CMAKE_CXX_COMPILER_ID:STRING=GNU\n"
  "CMAKE_CXX_COMPILER:FILEPATH=/usr/bin/g++\n")

find_program(_python3_program NAMES python3 python)
if(NOT _python3_program)
  message(FATAL_ERROR "coverage_report.py smoke requires python3 or python in PATH")
endif()
if(WIN32)
  set(_path_sep ";")
else()
  set(_path_sep ":")
endif()
set(_path_env "${_fake_tool_dir}${_path_sep}$ENV{PATH}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PYTHONPATH=${_fake_module_dir}"
    "PATH=${_path_env}"
    "${_python3_program}" "${SOURCE_DIR}/scripts/coverage_report.py"
      --config "${_policy_file}"
      --build-dir "${_build_dir}"
      --output-dir "${_output_dir}"
      --gcov "${_python3_program}" "${_fake_tool_dir}/fake_gcov.py"
  RESULT_VARIABLE _report_rc
  OUTPUT_VARIABLE _report_out
  ERROR_VARIABLE _report_err)
if(NOT _report_rc EQUAL 0)
  message(FATAL_ERROR
    "coverage_report.py smoke run failed.\n"
    "stdout:\n${_report_out}\n"
    "stderr:\n${_report_err}")
endif()

foreach(_required_file IN ITEMS
    "${_output_dir}/summary.json"
    "${_output_dir}/summary.md"
    "${_output_dir}/index.html"
    "${_gcovr_log}")
  if(NOT EXISTS "${_required_file}")
    message(FATAL_ERROR "Expected smoke output missing: ${_required_file}")
  endif()
endforeach()

file(READ "${_gcovr_log}" _gcovr_args)
if(NOT _gcovr_args MATCHES "src/intentional\\(/\\|\\$\\)")
  message(FATAL_ERROR
    "coverage_report.py did not pass intentional exclusions to gcovr with prefix semantics.\n"
    "Captured args:\n${_gcovr_args}")
endif()

file(REMOVE "${_gcovr_log}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PYTHONPATH=${_fake_module_dir}"
    "PATH=${_path_env}"
    "${_python3_program}" "${SOURCE_DIR}/scripts/coverage_report.py"
      --config "${_policy_file}"
      --build-dir "${_build_dir}"
      --output-dir "${_clang_auto_output_dir}"
  RESULT_VARIABLE _auto_rc
  OUTPUT_VARIABLE _auto_out
  ERROR_VARIABLE _auto_err)
if(NOT _auto_rc EQUAL 0)
  message(FATAL_ERROR
    "coverage_report.py smoke failed for the default gcov auto-selection path.\n"
    "stdout:\n${_auto_out}\n"
    "stderr:\n${_auto_err}")
endif()
file(READ "${_gcovr_log}" _auto_gcovr_args)
if(NOT _auto_gcovr_args MATCHES "llvm-cov")
  message(FATAL_ERROR
    "coverage_report.py did not auto-select llvm-cov gcov for a Clang build directory.\n"
    "Captured args:\n${_auto_gcovr_args}")
endif()
if(NOT EXISTS "${_clang_auto_output_dir}/summary.md" OR NOT EXISTS "${_clang_auto_output_dir}/index.html")
  message(FATAL_ERROR
    "Clang default gcov auto-selection smoke did not produce the expected report outputs.")
endif()

file(REMOVE "${_gcovr_log}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PYTHONPATH=${_fake_module_dir}"
    "PATH=${_path_env}"
    "${_python3_program}" "${SOURCE_DIR}/scripts/coverage_report.py"
      --config "${_policy_file}"
      --build-dir "${_gcc_build_dir}"
      --output-dir "${_gcc_auto_output_dir}"
  RESULT_VARIABLE _gcc_auto_rc
  OUTPUT_VARIABLE _gcc_auto_out
  ERROR_VARIABLE _gcc_auto_err)
if(NOT _gcc_auto_rc EQUAL 0)
  message(FATAL_ERROR
    "coverage_report.py smoke failed for the GCC default gcov auto-selection path.\n"
    "stdout:\n${_gcc_auto_out}\n"
    "stderr:\n${_gcc_auto_err}")
endif()
file(READ "${_gcovr_log}" _gcc_auto_gcovr_args)
if(NOT _gcc_auto_gcovr_args MATCHES "gcov")
  message(FATAL_ERROR
    "coverage_report.py did not auto-select gcov for a GNU build directory.\n"
    "Captured args:\n${_gcc_auto_gcovr_args}")
endif()

if(NOT WIN32)
  file(REMOVE "${_gcovr_log}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
      "PYTHONPATH=${_fake_module_dir}"
      "PATH=${_path_env}"
      "${_python3_program}" "${SOURCE_DIR}/scripts/coverage_report.py"
        --config "${_policy_file}"
        --build-dir "${_build_dir}"
        --output-dir "${_windows_style_output_dir}"
        --gcov "${_windows_style_llvm_cov}" gcov
    RESULT_VARIABLE _windows_style_rc
    OUTPUT_VARIABLE _windows_style_out
    ERROR_VARIABLE _windows_style_err)
  if(NOT _windows_style_rc EQUAL 0)
    message(FATAL_ERROR
      "coverage_report.py smoke failed for an explicit gcov frontend path containing Windows-style backslashes.\n"
      "stdout:\n${_windows_style_out}\n"
      "stderr:\n${_windows_style_err}")
  endif()
  file(READ "${_gcovr_log}" _windows_style_gcovr_args)
  if(NOT _windows_style_gcovr_args MATCHES "gcov_executable_parsed")
    message(FATAL_ERROR
      "coverage_report.py smoke did not capture the parsed gcov executable round-trip.\n"
      "Captured args:\n${_windows_style_gcovr_args}")
  endif()
  if(NOT _windows_style_gcovr_args MATCHES "C:\\\\\\\\Tools\\\\\\\\llvm\\\\\\\\bin\\\\\\\\llvm-cov")
    message(FATAL_ERROR
      "coverage_report.py did not preserve the Windows-style gcov frontend path through the gcovr-style shlex round-trip.\n"
      "Captured args:\n${_windows_style_gcovr_args}")
  endif()
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PYTHONPATH=${_fake_module_dir}"
    "PATH=${_path_env}"
    "${_python3_program}" "${SOURCE_DIR}/scripts/coverage_report.py"
      --config "${_policy_file}"
      --build-dir "${_build_dir}"
      --output-dir "${_threshold_output_dir}"
      --gcov "${_python3_program}" "${_fake_tool_dir}/fake_gcov.py"
      --fail-under-branch 45.0
  RESULT_VARIABLE _threshold_rc
  OUTPUT_VARIABLE _threshold_out
  ERROR_VARIABLE _threshold_err)
if(NOT _threshold_rc EQUAL 1)
  message(FATAL_ERROR
    "coverage_report.py threshold smoke should fail with exit code 1 after preserving report outputs.\n"
    "stdout:\n${_threshold_out}\n"
    "stderr:\n${_threshold_err}")
endif()
foreach(_threshold_file IN ITEMS "${_threshold_output_dir}/summary.md" "${_threshold_output_dir}/index.html")
  if(NOT EXISTS "${_threshold_file}")
    message(FATAL_ERROR
      "coverage_report.py did not preserve report outputs on a threshold failure: ${_threshold_file}")
  endif()
endforeach()

file(WRITE "${_gcovr_fail_output_dir}/summary.md" "stale\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PYTHONPATH=${_fake_module_dir}"
    "PATH=${_path_env}"
    "FAKE_GCOVR_MODE=fail"
    "${_python3_program}" "${SOURCE_DIR}/scripts/coverage_report.py"
      --config "${_policy_file}"
      --build-dir "${_build_dir}"
      --output-dir "${_gcovr_fail_output_dir}"
      --gcov "${_python3_program}" "${_fake_tool_dir}/fake_gcov.py"
  RESULT_VARIABLE _gcovr_fail_rc
  OUTPUT_VARIABLE _gcovr_fail_out
  ERROR_VARIABLE _gcovr_fail_err)
if(NOT _gcovr_fail_rc EQUAL 2)
  message(FATAL_ERROR
    "coverage_report.py smoke should surface the fake gcovr failure after temp output setup.\n"
    "stdout:\n${_gcovr_fail_out}\n"
    "stderr:\n${_gcovr_fail_err}")
endif()
if(EXISTS "${_gcovr_fail_output_dir}/summary.md")
  message(FATAL_ERROR
    "coverage_report.py preserved stale output after a fake gcovr failure.\n"
    "stdout:\n${_gcovr_fail_out}\n"
    "stderr:\n${_gcovr_fail_err}")
endif()
file(GLOB _gcovr_fail_tmp_dirs LIST_DIRECTORIES TRUE "${_gcovr_fail_output_dir}.tmp.*")
if(_gcovr_fail_tmp_dirs)
  message(FATAL_ERROR
    "coverage_report.py leaked temporary output directories after a fake gcovr failure: ${_gcovr_fail_tmp_dirs}")
endif()

file(WRITE "${_output_dir}/summary.md" "stale\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PYTHONPATH=${_fake_module_dir}"
    "${_python3_program}" "${SOURCE_DIR}/scripts/coverage_report.py"
      --config "${_policy_file}"
      --build-dir "${_build_dir}"
      --output-dir "${_output_dir}"
      --gcov /definitely/missing/gcov
  RESULT_VARIABLE _bad_rc
  OUTPUT_VARIABLE _bad_out
  ERROR_VARIABLE _bad_err)
if(_bad_rc EQUAL 0)
  message(FATAL_ERROR "coverage_report.py stale-output smoke unexpectedly succeeded with a missing gcov frontend.")
endif()
if(EXISTS "${_output_dir}" OR EXISTS "${_output_dir}/summary.md")
  message(FATAL_ERROR
    "coverage_report.py left stale output_dir contents behind after an early gcov frontend failure.\n"
    "stdout:\n${_bad_out}\n"
    "stderr:\n${_bad_err}")
endif()

file(GLOB _tmp_dirs LIST_DIRECTORIES TRUE "${_output_dir}.tmp.*")
if(_tmp_dirs)
  message(FATAL_ERROR
    "coverage_report.py leaked temporary output directories after failure: ${_tmp_dirs}")
endif()
