if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckMeasuredReportCompareScript.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckMeasuredReportCompareScript.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED Python3_EXECUTABLE OR "${Python3_EXECUTABLE}" STREQUAL "")
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
endif()

set(_script "${SOURCE_DIR}/scripts/compare_measured_reports.py")
if(NOT EXISTS "${_script}")
  message(FATAL_ERROR "Missing measured report comparison script: ${_script}")
endif()

set(_work_dir "${BUILD_ROOT}/measured_report_compare")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_baseline_json "${_work_dir}/baseline.json")
set(_current_json "${_work_dir}/current.json")
set(_empty_json "${_work_dir}/empty.json")
set(_baseline_csv "${_work_dir}/baseline.csv")
set(_current_csv "${_work_dir}/current.csv")
set(_truncated_csv "${_work_dir}/truncated.csv")
set(_zero_baseline_json "${_work_dir}/zero-baseline.json")
set(_zero_current_json "${_work_dir}/zero-current.json")
set(_missing_metric_json "${_work_dir}/missing-metric.json")
set(_summary_md "${_work_dir}/summary.md")

file(WRITE "${_baseline_json}" [=[
{"report":"measured","tables":[{"report":"bench","id":"bench.summary","title":"Benchmarks","rows":[{"benchmark":"bench/fast","suite":"bench","items_per_call":1,"median_ns_per_item":100.0,"p95_ns_per_item":120.0},{"benchmark":"bench/missing|edge","suite":"bench","items_per_call":1,"median_ns_per_item":50.0,"p95_ns_per_item":55.0}]},{"report":"jitter","id":"jitter.summary","title":"Jitter summary","rows":[{"benchmark":"jitter/noise","suite":"jitter","items_per_call":1,"median_ns_per_item":10.0,"p95_ns_per_item":12.0,"stddev_ns_per_item":2.0}]}],"issues":[]}
]=])

file(WRITE "${_current_json}" [=[
{"report":"measured","tables":[{"report":"bench","id":"bench.summary","title":"Benchmarks","rows":[{"benchmark":"bench/fast","suite":"bench","items_per_call":1,"median_ns_per_item":112.0,"p95_ns_per_item":118.0},{"benchmark":"bench/new|edge","suite":"bench","items_per_call":1,"median_ns_per_item":20.0,"p95_ns_per_item":25.0}]},{"report":"jitter","id":"jitter.summary","title":"Jitter summary","rows":[{"benchmark":"jitter/noise","suite":"jitter","items_per_call":1,"median_ns_per_item":9.0,"p95_ns_per_item":11.0,"stddev_ns_per_item":2.4}]}],"issues":[]}
]=])

file(WRITE "${_empty_json}" [=[
{"report":"measured","tables":[],"issues":[]}
]=])

file(WRITE "${_baseline_csv}" [=[report,table,row,field,type,value
bench,bench.summary,0,benchmark,string,bench/csv|quoted
bench,bench.summary,0,suite,string,"bench
csv"
bench,bench.summary,0,median_ns_per_item,number,100
bench,bench.summary,0,p95_ns_per_item,number,110
]=])

file(WRITE "${_current_csv}" [=[report,table,row,field,type,value
bench,bench.summary,0,benchmark,string,bench/csv|quoted
bench,bench.summary,0,suite,string,"bench
csv"
bench,bench.summary,0,median_ns_per_item,number,111
bench,bench.summary,0,p95_ns_per_item,number,109
]=])

file(WRITE "${_truncated_csv}" [=[report,table,row,field,type,value
bench,bench.summary
]=])

file(WRITE "${_zero_baseline_json}" [=[
{"report":"measured","tables":[{"report":"jitter","id":"jitter.summary","title":"Jitter summary","rows":[{"benchmark":"jitter/zero","suite":"jitter","stddev_ns_per_item":0.0}]}],"issues":[]}
]=])

file(WRITE "${_zero_current_json}" [=[
{"report":"measured","tables":[{"report":"jitter","id":"jitter.summary","title":"Jitter summary","rows":[{"benchmark":"jitter/zero","suite":"jitter","stddev_ns_per_item":1.0}]}],"issues":[]}
]=])

file(WRITE "${_missing_metric_json}" [=[
{"report":"measured","tables":[{"report":"bench","id":"bench.summary","title":"Benchmarks","rows":[{"benchmark":"bench/fast","suite":"bench","items_per_call":1,"median_ns_per_item":112.0}]}],"issues":[]}
]=])

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_script}" --help
  RESULT_VARIABLE _help_rc
  OUTPUT_VARIABLE _help_out
  ERROR_VARIABLE _help_err)
if(NOT _help_rc EQUAL 0)
  message(FATAL_ERROR
    "compare_measured_reports.py --help failed.\n"
    "stdout:\n${_help_out}\n"
    "stderr:\n${_help_err}")
endif()
foreach(_required_help IN ITEMS "--baseline" "--current" "--fail-regression-pct" "--markdown-out" "--fail-on-new" "--fail-on-missing")
  string(FIND "${_help_out}" "${_required_help}" _help_pos)
  if(_help_pos EQUAL -1)
    message(FATAL_ERROR "compare_measured_reports.py --help must document ${_required_help}.")
  endif()
endforeach()

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_script}"
    --baseline "${_zero_baseline_json}"
    --current "${_zero_current_json}"
    --metric stddev_ns_per_item
    --fail-regression-pct 10
  RESULT_VARIABLE _zero_fail_rc
  OUTPUT_VARIABLE _zero_fail_out
  ERROR_VARIABLE _zero_fail_err)
if(NOT _zero_fail_rc EQUAL 1 OR NOT _zero_fail_out MATCHES "\\+inf%")
  message(FATAL_ERROR
    "A positive metric increase from zero must be an infinite-percentage regression.\n"
    "stdout:\n${_zero_fail_out}\n"
    "stderr:\n${_zero_fail_err}")
endif()

foreach(_missing_baseline IN ITEMS FALSE TRUE)
  if(_missing_baseline)
    set(_metric_baseline "${_missing_metric_json}")
    set(_metric_current "${_baseline_json}")
    set(_missing_side "baseline")
  else()
    set(_metric_baseline "${_baseline_json}")
    set(_metric_current "${_missing_metric_json}")
    set(_missing_side "current")
  endif()
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${_script}"
      --baseline "${_metric_baseline}"
      --current "${_metric_current}"
      --metric p95_ns_per_item
    RESULT_VARIABLE _metric_fail_rc
    OUTPUT_VARIABLE _metric_fail_out
    ERROR_VARIABLE _metric_fail_err)
  if(NOT _metric_fail_rc EQUAL 2 OR NOT _metric_fail_err MATCHES "missing from the ${_missing_side} report")
    message(FATAL_ERROR
      "One-sided metric loss from the ${_missing_side} report must be an input error.\n"
      "stdout:\n${_metric_fail_out}\n"
      "stderr:\n${_metric_fail_err}")
  endif()
endforeach()

foreach(_threshold IN ITEMS nan inf -inf)
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${_script}"
      --baseline "${_baseline_json}"
      --current "${_current_json}"
      "--fail-regression-pct=${_threshold}"
    RESULT_VARIABLE _threshold_fail_rc
    OUTPUT_VARIABLE _threshold_fail_out
    ERROR_VARIABLE _threshold_fail_err)
  if(NOT _threshold_fail_rc EQUAL 2 OR NOT _threshold_fail_err MATCHES "finite and non-negative")
    message(FATAL_ERROR
      "Non-finite threshold '${_threshold}' must be rejected.\n"
      "stdout:\n${_threshold_fail_out}\n"
      "stderr:\n${_threshold_fail_err}")
  endif()
endforeach()

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_script}"
    --baseline "${_truncated_csv}"
    --current "${_current_csv}"
  RESULT_VARIABLE _truncated_fail_rc
  OUTPUT_VARIABLE _truncated_fail_out
  ERROR_VARIABLE _truncated_fail_err)
if(NOT _truncated_fail_rc EQUAL 2 OR NOT _truncated_fail_err MATCHES "malformed CSV record")
  message(FATAL_ERROR
    "Truncated CSV rows must use the documented input-error path.\n"
    "stdout:\n${_truncated_fail_out}\n"
    "stderr:\n${_truncated_fail_err}")
endif()

if(UNIX)
  find_program(_bash_program bash)
  if(_bash_program)
    set(_wrapper_dir "${_work_dir}/wrapper")
    set(_fake_report "${_work_dir}/fake-report.sh")
    set(_fake_compare "${_work_dir}/fake-compare.py")
    file(WRITE "${_fake_report}" "#!/usr/bin/env bash\nprintf '%s\\n' '{\"report\":\"measured\",\"tables\":[],\"issues\":[]}'\n")
    file(CHMOD "${_fake_report}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    file(WRITE "${_fake_compare}" "raise SystemExit(37)\n")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env
        "GENTEST_MEASURED_BASE_EXE=${_fake_report}"
        "GENTEST_MEASURED_CURRENT_EXE=${_fake_report}"
        "GENTEST_MEASURED_COMPARE_SCRIPT=${_fake_compare}"
        "GENTEST_MEASURED_REPORT_DIR=${_wrapper_dir}"
        "${_bash_program}" "${SOURCE_DIR}/scripts/ci_measured_report_compare.sh"
      WORKING_DIRECTORY "${SOURCE_DIR}"
      RESULT_VARIABLE _wrapper_fail_rc
      OUTPUT_VARIABLE _wrapper_fail_out
      ERROR_VARIABLE _wrapper_fail_err)
    if(NOT _wrapper_fail_rc EQUAL 37)
      message(FATAL_ERROR
        "The CI wrapper must propagate unexpected comparator failures.\n"
        "stdout:\n${_wrapper_fail_out}\n"
        "stderr:\n${_wrapper_fail_err}")
    endif()

    file(REMOVE_RECURSE "${_wrapper_dir}")
    file(WRITE "${_fake_compare}" "raise SystemExit(1)\n")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env
        "GENTEST_MEASURED_BASE_EXE=${_fake_report}"
        "GENTEST_MEASURED_CURRENT_EXE=${_fake_report}"
        "GENTEST_MEASURED_COMPARE_SCRIPT=${_fake_compare}"
        "GENTEST_MEASURED_REPORT_DIR=${_wrapper_dir}"
        "${_bash_program}" "${SOURCE_DIR}/scripts/ci_measured_report_compare.sh"
      WORKING_DIRECTORY "${SOURCE_DIR}"
      RESULT_VARIABLE _wrapper_no_summary_rc
      OUTPUT_VARIABLE _wrapper_no_summary_out
      ERROR_VARIABLE _wrapper_no_summary_err)
    if(NOT _wrapper_no_summary_rc EQUAL 2 OR NOT _wrapper_no_summary_err MATCHES "without a summary")
      message(FATAL_ERROR
        "Regression status without a summary must be rejected as an execution failure.\n"
        "stdout:\n${_wrapper_no_summary_out}\n"
        "stderr:\n${_wrapper_no_summary_err}")
    endif()
  endif()
endif()

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_script}"
    --baseline "${_empty_json}"
    --current "${_current_json}"
  RESULT_VARIABLE _empty_fail_rc
  OUTPUT_VARIABLE _empty_fail_out
  ERROR_VARIABLE _empty_fail_err)
if(NOT _empty_fail_rc EQUAL 2)
  message(FATAL_ERROR
    "Comparison should reject reports without measured summary rows.\n"
    "stdout:\n${_empty_fail_out}\n"
    "stderr:\n${_empty_fail_err}")
endif()
string(FIND "${_empty_fail_err}" "contains no measured summary rows" _empty_error_pos)
if(_empty_error_pos EQUAL -1)
  message(FATAL_ERROR
    "Empty report failure did not explain the missing measured rows.\n"
    "stderr:\n${_empty_fail_err}")
endif()

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_script}"
    --baseline "${_baseline_json}"
    --current "${_current_json}"
    --fail-regression-pct 10
    --markdown-out "${_summary_md}"
  RESULT_VARIABLE _json_fail_rc
  OUTPUT_VARIABLE _json_fail_out
  ERROR_VARIABLE _json_fail_err)
if(NOT _json_fail_rc EQUAL 1)
  message(FATAL_ERROR
    "JSON comparison should fail on regressions over threshold.\n"
    "stdout:\n${_json_fail_out}\n"
    "stderr:\n${_json_fail_err}")
endif()
if(NOT EXISTS "${_summary_md}")
  message(FATAL_ERROR "JSON comparison did not write requested Markdown summary: ${_summary_md}")
endif()
file(READ "${_summary_md}" _json_summary)
foreach(_required_summary IN ITEMS "Regressions" "bench/fast" "jitter/noise" "New Benchmarks" "bench/new&#124;edge" "Missing Benchmarks" "bench/missing&#124;edge")
  string(FIND "${_json_summary}" "${_required_summary}" _summary_pos)
  if(_summary_pos EQUAL -1)
    message(FATAL_ERROR
      "JSON comparison summary missing '${_required_summary}'.\n"
      "summary.md:\n${_json_summary}")
  endif()
endforeach()

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_script}"
    --baseline "${_baseline_json}"
    --current "${_current_json}"
    --fail-regression-pct 25
  RESULT_VARIABLE _json_pass_rc
  OUTPUT_VARIABLE _json_pass_out
  ERROR_VARIABLE _json_pass_err)
if(NOT _json_pass_rc EQUAL 0)
  message(FATAL_ERROR
    "JSON comparison should not fail below regression threshold or for reported-only new/missing benchmarks.\n"
    "stdout:\n${_json_pass_out}\n"
    "stderr:\n${_json_pass_err}")
endif()
foreach(_required_output IN ITEMS "New Benchmarks" "Missing Benchmarks" "bench/new&#124;edge" "bench/missing&#124;edge")
  string(FIND "${_json_pass_out}" "${_required_output}" _json_pass_pos)
  if(_json_pass_pos EQUAL -1)
    message(FATAL_ERROR
      "Passing JSON comparison did not report '${_required_output}'.\n"
      "stdout:\n${_json_pass_out}")
  endif()
endforeach()

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_script}"
    --baseline "${_baseline_json}"
    --current "${_current_json}"
    --fail-regression-pct 25
    --fail-on-new
  RESULT_VARIABLE _new_fail_rc
  OUTPUT_VARIABLE _new_fail_out
  ERROR_VARIABLE _new_fail_err)
if(NOT _new_fail_rc EQUAL 1)
  message(FATAL_ERROR
    "JSON comparison with --fail-on-new should fail when current report has new benchmarks.\n"
    "stdout:\n${_new_fail_out}\n"
    "stderr:\n${_new_fail_err}")
endif()

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_script}"
    --baseline "${_baseline_csv}"
    --current "${_current_csv}"
    --metric median_ns_per_item
    --fail-regression-pct 10
  RESULT_VARIABLE _csv_fail_rc
  OUTPUT_VARIABLE _csv_fail_out
  ERROR_VARIABLE _csv_fail_err)
if(NOT _csv_fail_rc EQUAL 1)
  message(FATAL_ERROR
    "CSV comparison should fail on median_ns_per_item regression over threshold.\n"
    "stdout:\n${_csv_fail_out}\n"
    "stderr:\n${_csv_fail_err}")
endif()
foreach(_required_csv_output IN ITEMS "bench/csv&#124;quoted" "median_ns_per_item" "+11.00%")
  string(FIND "${_csv_fail_out}" "${_required_csv_output}" _csv_pos)
  if(_csv_pos EQUAL -1)
    message(FATAL_ERROR
      "CSV comparison output missing '${_required_csv_output}'.\n"
      "stdout:\n${_csv_fail_out}")
  endif()
endforeach()
