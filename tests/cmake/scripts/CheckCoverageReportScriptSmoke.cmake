if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCoverageReportScriptSmoke.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckCoverageReportScriptSmoke.cmake: BUILD_ROOT not set")
endif()

if(WIN32)
  message("GENTEST_SKIP_TEST: coverage report smoke is Linux/POSIX scoped")
  return()
endif()

include("${SOURCE_DIR}/tests/cmake/scripts/CheckFixtureWriteHelpers.cmake")

find_program(_python3_program NAMES python3 python)
if(NOT _python3_program)
  message(FATAL_ERROR "coverage_report.py smoke requires python3 or python in PATH")
endif()

set(_work_dir "${BUILD_ROOT}/coverage_report_smoke")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_build_dir "${_work_dir}/build")
set(_fake_module_dir "${_work_dir}/fake_module")
set(_fake_tool_dir "${_work_dir}/fake_tools")
set(_default_script_dir "${_work_dir}/default_policy_copy")
set(_policy_file "${_work_dir}/coverage_policy.toml")
set(_default_policy_file "${_default_script_dir}/coverage_hygiene.toml")
set(_gcovr_log "${_work_dir}/gcovr_args.json")
set(_gcovr_log_default "${_work_dir}/gcovr_args_default.json")
set(_gcovr_log_policy "${_work_dir}/gcovr_args_policy.json")
set(_gcovr_log_override "${_work_dir}/gcovr_args_override.json")
set(_fake_gcov "${_fake_tool_dir}/fake_gcov.py")
set(_fake_llvm_cov "${_fake_tool_dir}/llvm-cov")
file(MAKE_DIRECTORY "${_build_dir}" "${_fake_module_dir}" "${_fake_tool_dir}" "${_default_script_dir}")
configure_file("${SOURCE_DIR}/scripts/coverage_report.py" "${_default_script_dir}/coverage_report.py" COPYONLY)
configure_file("${SOURCE_DIR}/scripts/coverage_hygiene.py" "${_default_script_dir}/coverage_hygiene.py" COPYONLY)

gentest_fixture_write_file(
  "${_fake_gcov}"
  "import sys\n"
  "raise SystemExit(0)\n")
gentest_fixture_write_file(
  "${_fake_llvm_cov}"
  "#!/bin/sh\n"
  "exit 0\n")
file(CHMOD "${_fake_llvm_cov}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
gentest_fixture_write_file(
  "${_build_dir}/CMakeCache.txt"
  "CMAKE_CXX_COMPILER_ID:STRING=Clang\n"
  "CMAKE_CXX_COMPILER:FILEPATH=${_fake_tool_dir}/clang++\n")

gentest_fixture_write_file(
  "${_fake_module_dir}/gcovr.py"
  "import json\n"
  "import os\n"
  "import pathlib\n"
  "import sys\n"
  "\n"
  "args = sys.argv[1:]\n"
  "payload = {'args': args}\n"
  "log_path = pathlib.Path(os.environ.get('FAKE_GCOVR_LOG_PATH', r'${_gcovr_log}'))\n"
  "log_path.write_text(json.dumps(payload, indent=2), encoding='utf-8')\n"
  "\n"
  "summary = None\n"
  "html = None\n"
  "for index, arg in enumerate(args):\n"
  "    if arg == '--json-summary' and index + 1 < len(args):\n"
  "        summary = pathlib.Path(args[index + 1])\n"
  "    elif arg == '--html-details' and index + 1 < len(args):\n"
  "        html = pathlib.Path(args[index + 1])\n"
  "if summary is None or html is None:\n"
  "    raise SystemExit('missing gcovr output arguments')\n"
  "\n"
  "line_percent = float(os.environ.get('FAKE_GCOVR_LINE_PERCENT', '80.0'))\n"
  "branch_percent = float(os.environ.get('FAKE_GCOVR_BRANCH_PERCENT', '50.0'))\n"
  "summary.parent.mkdir(parents=True, exist_ok=True)\n"
  "html.parent.mkdir(parents=True, exist_ok=True)\n"
  "summary.write_text(json.dumps({\n"
  "    'line_covered': 8,\n"
  "    'line_total': 10,\n"
  "    'line_percent': line_percent,\n"
  "    'branch_covered': 5,\n"
  "    'branch_total': 10,\n"
  "    'branch_percent': branch_percent,\n"
  "    'function_covered': 4,\n"
  "    'function_total': 5,\n"
  "    'function_percent': 80.0,\n"
  "    'files': [\n"
  "        {\n"
  "            'filename': 'src/demo.cpp',\n"
  "            'line_percent': line_percent,\n"
  "            'branch_percent': branch_percent,\n"
  "            'function_percent': 80.0,\n"
  "        },\n"
  "        {\n"
  "            'filename': 'tools/src/detail.hpp',\n"
  "            'line_percent': 70.0,\n"
  "            'branch_percent': 35.0,\n"
  "            'function_percent': 100.0,\n"
  "        }\n"
  "    ],\n"
  "}, indent=2), encoding='utf-8')\n"
  "html.write_text('<html><body>fake gcovr</body></html>\\n', encoding='utf-8')\n")

gentest_fixture_write_file(
  "${_policy_file}"
  "roots = [\"src\", \"tools/src\"]\n"
  "exclude_prefix = [\"src/excluded\"]\n"
  "intentional = [\"src/gentest_anchor.cpp\"]\n"
  "no_exec = []\n"
  "fail_on = \"missing_obj,missing_gcda,stamp_mismatch,no_match,gcov_error\"\n"
  "warn_on = \"zero_hits\"\n"
  "\n"
  "[report]\n"
  "fail_under_line = 75.0\n"
  "fail_under_branch = 45.0\n"
  "top_files = 5\n")
gentest_fixture_write_file(
  "${_default_policy_file}"
  "roots = [\"src\", \"tools/src\"]\n"
  "exclude_prefix = []\n"
  "intentional = []\n"
  "no_exec = []\n"
  "fail_on = \"missing_obj,missing_gcda,stamp_mismatch,no_match,gcov_error\"\n"
  "warn_on = \"zero_hits\"\n"
  "\n"
  "[report]\n"
  "fail_under_line = 60.0\n"
  "fail_under_branch = 47.0\n"
  "top_files = 2\n")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PYTHONPATH=${_fake_module_dir}"
    "PATH=${_fake_tool_dir}:$ENV{PATH}"
    "FAKE_GCOVR_LOG_PATH=${_gcovr_log}"
    "${_python3_program}" "${SOURCE_DIR}/scripts/coverage_report.py"
      --config "${_policy_file}"
      --build-dir "${_build_dir}"
  RESULT_VARIABLE _report_rc
  OUTPUT_VARIABLE _report_out
  ERROR_VARIABLE _report_err)
if(NOT _report_rc EQUAL 0)
  message(FATAL_ERROR
    "coverage_report.py smoke run failed.\n"
    "stdout:\n${_report_out}\n"
    "stderr:\n${_report_err}")
endif()

set(_output_dir "${_build_dir}/coverage-report")
foreach(_required_file IN ITEMS
    "${_output_dir}/summary.json"
    "${_output_dir}/summary.md"
    "${_output_dir}/index.html"
    "${_gcovr_log}")
  if(NOT EXISTS "${_required_file}")
    message(FATAL_ERROR "Expected coverage_report.py smoke output missing: ${_required_file}")
  endif()
endforeach()

file(READ "${_gcovr_log}" _gcovr_args_text)
execute_process(
  COMMAND "${_python3_program}" -c
    "import json, sys; "
    "args = json.load(open(sys.argv[1], encoding='utf-8'))['args']; "
    "gcov_values = [args[i + 1] for i, arg in enumerate(args[:-1]) if arg == '--gcov-executable']; "
    "filters = [args[i + 1] for i, arg in enumerate(args[:-1]) if arg == '--filter']; "
    "excludes = [args[i + 1] for i, arg in enumerate(args[:-1]) if arg == '--exclude']; "
    "missing = []; "
    "missing.extend([f'gcov:{value}' for value in ['${_fake_llvm_cov} gcov'] if value not in gcov_values]); "
    "checks = ["
    "('filter', '/src(/|$)', filters), "
    "('filter', '/tools/src(/|$)', filters), "
    "('exclude', '/src/excluded(/|$)', excludes), "
    "('exclude', '/src/gentest_anchor\\\\.cpp$', excludes)"
    "]; "
    "missing.extend([f'{kind}:{suffix}' for kind, suffix, values in checks if not any(value.endswith(suffix) for value in values)]); "
    "print('\\n'.join(missing)); "
    "raise SystemExit(1 if missing else 0)"
    "${_gcovr_log}"
  RESULT_VARIABLE _validate_args_rc
  OUTPUT_VARIABLE _validate_args_out
  ERROR_VARIABLE _validate_args_err)
if(NOT _validate_args_rc EQUAL 0)
  message(FATAL_ERROR
    "coverage_report.py smoke did not pass expected scope/exclusion args to gcovr.\n"
    "stdout:\n${_validate_args_out}\n"
    "stderr:\n${_validate_args_err}\n"
    "Captured args:\n${_gcovr_args_text}")
endif()

file(READ "${_output_dir}/summary.md" _summary_md)
string(FIND "${_summary_md}" "`tools/src/detail.hpp`" _weakest_file_pos)
if(_weakest_file_pos EQUAL -1)
  message(FATAL_ERROR
    "coverage_report.py smoke did not include the per-file HTML/Markdown summary output as expected.\n"
    "summary.md contents:\n${_summary_md}")
endif()

set(_default_output_dir "${_work_dir}/default-coverage-report")
file(REMOVE_RECURSE "${_default_output_dir}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PYTHONPATH=${_fake_module_dir}"
    "PATH=${_fake_tool_dir}:$ENV{PATH}"
    "FAKE_GCOVR_LOG_PATH=${_gcovr_log_default}"
    "FAKE_GCOVR_LINE_PERCENT=80.0"
    "FAKE_GCOVR_BRANCH_PERCENT=46.0"
    "${_python3_program}" "${_default_script_dir}/coverage_report.py"
      --build-dir "${_build_dir}"
      --output-dir "${_default_output_dir}"
  RESULT_VARIABLE _default_policy_rc
  OUTPUT_VARIABLE _default_policy_out
  ERROR_VARIABLE _default_policy_err)
if(NOT _default_policy_rc EQUAL 1)
  message(FATAL_ERROR
    "coverage_report.py default-policy smoke expected a threshold failure (rc=1).\n"
    "stdout:\n${_default_policy_out}\n"
    "stderr:\n${_default_policy_err}")
endif()

foreach(_required_file IN ITEMS "${_default_output_dir}/summary.md" "${_default_output_dir}/index.html" "${_gcovr_log_default}")
  if(NOT EXISTS "${_required_file}")
    message(FATAL_ERROR
      "coverage_report.py default-policy smoke must still write ${_required_file}.")
  endif()
endforeach()
string(FIND "${_default_policy_out}" "branch coverage 46.0% < required 47.0%" _default_policy_reason_pos)
if(_default_policy_reason_pos EQUAL -1)
  message(FATAL_ERROR
    "coverage_report.py default-policy smoke did not honor the adjacent default coverage_hygiene.toml policy.\n"
    "stdout:\n${_default_policy_out}\n"
    "stderr:\n${_default_policy_err}")
endif()

file(REMOVE_RECURSE "${_output_dir}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PYTHONPATH=${_fake_module_dir}"
    "PATH=${_fake_tool_dir}:$ENV{PATH}"
    "FAKE_GCOVR_LOG_PATH=${_gcovr_log_policy}"
    "FAKE_GCOVR_LINE_PERCENT=80.0"
    "FAKE_GCOVR_BRANCH_PERCENT=44.0"
    "${_python3_program}" "${SOURCE_DIR}/scripts/coverage_report.py"
      --config "${_policy_file}"
      --build-dir "${_build_dir}"
  RESULT_VARIABLE _policy_threshold_rc
  OUTPUT_VARIABLE _policy_threshold_out
  ERROR_VARIABLE _policy_threshold_err)
if(NOT _policy_threshold_rc EQUAL 1)
  message(FATAL_ERROR
    "coverage_report.py smoke expected a policy-driven threshold failure (rc=1).\n"
    "stdout:\n${_policy_threshold_out}\n"
    "stderr:\n${_policy_threshold_err}")
endif()

foreach(_required_file IN ITEMS "${_output_dir}/summary.md" "${_output_dir}/index.html")
  if(NOT EXISTS "${_required_file}")
    message(FATAL_ERROR
      "coverage_report.py policy-threshold smoke must still write ${_required_file} before returning non-zero.")
  endif()
endforeach()
string(FIND "${_policy_threshold_out}" "Coverage threshold failures:" _policy_threshold_failures_pos)
string(FIND "${_policy_threshold_out}" "branch coverage 44.0% < required 45.0%" _policy_threshold_reason_pos)
if(_policy_threshold_failures_pos EQUAL -1 OR _policy_threshold_reason_pos EQUAL -1)
  message(FATAL_ERROR
    "coverage_report.py policy-threshold smoke did not report the expected branch threshold message.\n"
    "stdout:\n${_policy_threshold_out}\n"
    "stderr:\n${_policy_threshold_err}")
endif()

file(REMOVE_RECURSE "${_output_dir}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PYTHONPATH=${_fake_module_dir}"
    "PATH=${_fake_tool_dir}:$ENV{PATH}"
    "FAKE_GCOVR_LOG_PATH=${_gcovr_log_override}"
    "FAKE_GCOVR_LINE_PERCENT=80.0"
    "FAKE_GCOVR_BRANCH_PERCENT=50.0"
    "${_python3_program}" "${SOURCE_DIR}/scripts/coverage_report.py"
      --config "${_policy_file}"
      --build-dir "${_build_dir}"
      --gcov "${_python3_program}" "${_fake_gcov}"
      --fail-under-line 81
  RESULT_VARIABLE _threshold_rc
  OUTPUT_VARIABLE _threshold_out
  ERROR_VARIABLE _threshold_err)
if(NOT _threshold_rc EQUAL 1)
  message(FATAL_ERROR
    "coverage_report.py smoke expected a threshold failure (rc=1).\n"
    "stdout:\n${_threshold_out}\n"
    "stderr:\n${_threshold_err}")
endif()

foreach(_required_file IN ITEMS "${_output_dir}/summary.md" "${_output_dir}/index.html")
  if(NOT EXISTS "${_required_file}")
    message(FATAL_ERROR
      "coverage_report.py threshold-failure smoke must still write ${_required_file} before returning non-zero.")
  endif()
endforeach()
file(READ "${_gcovr_log_override}" _gcovr_override_args_text)
execute_process(
  COMMAND "${_python3_program}" -c
    "import json, sys; "
    "args = json.load(open(sys.argv[1], encoding='utf-8'))['args']; "
    "gcov_values = [args[i + 1] for i, arg in enumerate(args[:-1]) if arg == '--gcov-executable']; "
    "expected = '${_python3_program} ${_fake_gcov}'; "
    "raise SystemExit(0 if expected in gcov_values else 1)"
    "${_gcovr_log_override}"
  RESULT_VARIABLE _override_args_rc
  OUTPUT_VARIABLE _override_args_out
  ERROR_VARIABLE _override_args_err)
if(NOT _override_args_rc EQUAL 0)
  message(FATAL_ERROR
    "coverage_report.py override smoke did not honor the explicit --gcov override.\n"
    "stdout:\n${_override_args_out}\n"
    "stderr:\n${_override_args_err}\n"
    "Captured args:\n${_gcovr_override_args_text}")
endif()
string(FIND "${_threshold_out}" "Coverage threshold failures:" _threshold_failures_pos)
string(FIND "${_threshold_out}" "line coverage 80.0% < required 81.0%" _threshold_reason_pos)
if(_threshold_failures_pos EQUAL -1 OR _threshold_reason_pos EQUAL -1)
  message(FATAL_ERROR
    "coverage_report.py threshold-failure smoke did not report the expected threshold message.\n"
    "stdout:\n${_threshold_out}\n"
    "stderr:\n${_threshold_err}")
endif()
