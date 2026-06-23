#!/usr/bin/env bash
set -euo pipefail

if [ -z "${GENTEST_MEASURED_BASE_EXE:-}" ]; then
  echo "error: GENTEST_MEASURED_BASE_EXE is not set" >&2
  exit 2
fi
if [ -z "${GENTEST_MEASURED_CURRENT_EXE:-}" ]; then
  echo "error: GENTEST_MEASURED_CURRENT_EXE is not set" >&2
  exit 2
fi
if [ ! -x "${GENTEST_MEASURED_BASE_EXE}" ]; then
  echo "error: baseline benchmark executable is not executable: ${GENTEST_MEASURED_BASE_EXE}" >&2
  exit 2
fi
if [ ! -x "${GENTEST_MEASURED_CURRENT_EXE}" ]; then
  echo "error: current benchmark executable is not executable: ${GENTEST_MEASURED_CURRENT_EXE}" >&2
  exit 2
fi

repo_root="$(git rev-parse --show-toplevel)"
report_dir="${GENTEST_MEASURED_REPORT_DIR:-${repo_root}/build/measured-report-compare}"
fail_pct="${GENTEST_MEASURED_FAIL_REGRESSION_PCT:-10}"

mkdir -p "${report_dir}"

baseline_json="${report_dir}/baseline-measured.json"
current_json="${report_dir}/current-measured.json"
summary_md="${report_dir}/measured-compare.md"
status_file="${report_dir}/comparison-exit-code.txt"

common_args=(
  --filter=benchmarks/math/*
  --kind=all
  --bench-epochs=1
  --bench-warmup=0
  --bench-min-epoch-time-s=0
  --bench-min-total-time-s=0
  --bench-max-total-time-s=0.02
  --jitter-bins=5
  --report-format=json
)

echo "[measured] writing baseline report: ${baseline_json}"
"${GENTEST_MEASURED_BASE_EXE}" "${common_args[@]}" > "${baseline_json}"

echo "[measured] writing current report: ${current_json}"
"${GENTEST_MEASURED_CURRENT_EXE}" "${common_args[@]}" > "${current_json}"

echo "[measured] comparing reports: ${summary_md}"
set +e
python3 "${repo_root}/scripts/compare_measured_reports.py" \
  --baseline "${baseline_json}" \
  --current "${current_json}" \
  --fail-regression-pct "${fail_pct}" \
  --markdown-out "${summary_md}"
compare_rc=$?
set -e

printf '%s\n' "${compare_rc}" > "${status_file}"

if [ "${compare_rc}" -eq 2 ]; then
  echo "error: measured report comparison failed to run" >&2
  exit 2
fi

if [ "${compare_rc}" -eq 1 ]; then
  echo "[measured] comparison found regressions over ${fail_pct}%; reporting only"
else
  echo "[measured] comparison completed without over-threshold regressions"
fi

echo "[measured] summary: ${summary_md}"
