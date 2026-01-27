#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Profile gentest code generation (gentest_codegen runs) to identify hotspots and per-target memory use.

Default behavior:
  - Configures a RelWithDebInfo + frame-pointer build in build/profile-system (system toolchain)
  - Builds gentest_codegen
  - Extracts all gentest_codegen custom commands from build.ninja
  - Records a perf profile over running all codegen commands (serial)
  - Runs each codegen command under /usr/bin/time -v to capture Max RSS

Outputs are written under:
  <build-dir>/profiling/codegen_<timestamp>.*

Usage:
  scripts/profile_codegen.sh [options]

Options:
  --build-dir <dir>         Build directory (default: build/profile-system)
  --preset <name>           Use an existing CMake preset instead of manual configure
  --fresh                   Re-configure the build directory
  --jobs <n>                Jobs for building gentest_codegen (default: 1)
  --freq <n>                perf sampling frequency (default: 999)
  --call-graph <mode>       perf call graph: none|fp|dwarf (default: none)
  --percent-limit <p>       perf report percent cutoff (default: 0.5)
  --cmake-arg <arg>         Extra arg appended to configure (repeatable)
  -h, --help                Show this help

Examples:
  scripts/profile_codegen.sh --fresh
  scripts/profile_codegen.sh --preset profile --fresh
  scripts/profile_codegen.sh --call-graph fp --percent-limit 0.1
EOF
}

build_dir="build/profile-system"
preset=""
fresh=0
jobs=1
perf_freq=999
perf_call_graph="none"
perf_percent_limit="0.5"
cmake_args=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      build_dir="$2"
      shift 2
      ;;
    --preset)
      preset="$2"
      shift 2
      ;;
    --fresh)
      fresh=1
      shift
      ;;
    --jobs)
      jobs="$2"
      shift 2
      ;;
    --freq)
      perf_freq="$2"
      shift 2
      ;;
    --call-graph)
      perf_call_graph="$2"
      shift 2
      ;;
    --percent-limit)
      perf_percent_limit="$2"
      shift 2
      ;;
    --cmake-arg)
      cmake_args+=("$2")
      shift 2
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if ! command -v cmake >/dev/null; then
  echo "error: cmake not found" >&2
  exit 1
fi
if ! command -v ninja >/dev/null; then
  echo "error: ninja not found" >&2
  exit 1
fi
if ! command -v perf >/dev/null; then
  echo "error: perf not found" >&2
  exit 1
fi
if ! command -v python3 >/dev/null; then
  echo "error: python3 not found" >&2
  exit 1
fi
if [[ "$perf_call_graph" != "none" && "$perf_call_graph" != "fp" && "$perf_call_graph" != "dwarf" ]]; then
  echo "error: --call-graph must be one of: none|fp|dwarf" >&2
  exit 2
fi

export CCACHE_DISABLE=1

if [[ -n "$preset" ]]; then
  if (( fresh )); then
    cmake --preset="$preset" --fresh "${cmake_args[@]}"
  else
    cmake --preset="$preset" "${cmake_args[@]}"
  fi
  build_dir="build/${preset}"
  cmake --build --preset="$preset" --target gentest_codegen -j "${jobs}"
else
  if (( fresh )) || [[ ! -f "${build_dir}/build.ninja" ]]; then
    cmake -S . -B "${build_dir}" -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_TOOLCHAIN_FILE= \
      -DCMAKE_CXX_EXTENSIONS=OFF \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -DGENTEST_ENABLE_PACKAGE_TESTS=ON \
      -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer" \
      -DCMAKE_C_FLAGS="-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer" \
      "${cmake_args[@]}"
  fi
  cmake --build "${build_dir}" --target gentest_codegen -j "${jobs}"
fi

timestamp="$(date +%Y%m%d_%H%M%S)"
out_dir="${build_dir}/profiling"
mkdir -p "${out_dir}"
prefix="${out_dir}/codegen_${timestamp}"

{
  echo "timestamp=${timestamp}"
  echo "git_rev=$(git rev-parse HEAD 2>/dev/null || echo unknown)"
  echo "build_dir=${build_dir}"
  echo "preset=${preset:-manual}"
  echo "jobs=${jobs}"
  echo "perf=$(perf --version | head -n1)"
  echo "ninja=$(ninja --version | head -n1)"
  echo "cmake=$(cmake --version | head -n1)"
  echo "uname=$(uname -a)"
} > "${prefix}.meta.txt"

python3 - "${build_dir}" "${prefix}.commands.json" "${prefix}.commands.sh" <<'PY'
import json
import re
import sys
from pathlib import Path

build_dir = Path(sys.argv[1])
out_json = Path(sys.argv[2])
out_sh = Path(sys.argv[3])

build_ninja = build_dir / "build.ninja"
if not build_ninja.exists():
    raise SystemExit(f"error: build.ninja not found: {build_ninja}")

running_re = re.compile(r"^\s*DESC\s*=\s*Running gentest_codegen for target\s+([\w_]+)\s*$")
cmd_re = re.compile(r"^\s*COMMAND\s*=\s*(.+)$")

commands: list[dict[str, str]] = []
current_cmd: str | None = None

for line in build_ninja.read_text(encoding="utf-8", errors="replace").splitlines():
    if line.startswith("build "):
        current_cmd = None
        continue
    m = cmd_re.match(line)
    if m:
        current_cmd = m.group(1)
        continue
    m = running_re.match(line)
    if m and current_cmd:
        commands.append({"target": m.group(1), "command": current_cmd})

if not commands:
    raise SystemExit("error: no gentest_codegen commands found (expected 'DESC = Running gentest_codegen for target ...')")

out_json.write_text(json.dumps({"commands": commands}, indent=2) + "\n", encoding="utf-8")

out_sh.write_text(
    "#!/usr/bin/env bash\n"
    "set -euo pipefail\n"
    "# Auto-generated by scripts/profile_codegen.sh\n"
    + "".join(f"{entry['command']}\n" for entry in commands),
    encoding="utf-8",
)
print(f"[profile] Extracted {len(commands)} codegen commands -> {out_json} / {out_sh}")
PY

chmod +x "${prefix}.commands.sh"

perf_args=(record -o "${prefix}.perf.data" -F "${perf_freq}")
if [[ "${perf_call_graph}" != "none" ]]; then
  perf_args+=(--call-graph "${perf_call_graph}")
fi
perf_args+=(-- bash "${prefix}.commands.sh")

echo "[profile] Recording perf profile..."
perf "${perf_args[@]}"

echo "[profile] Writing perf report..."
perf report -i "${prefix}.perf.data" \
  --stdio \
  --no-children \
  --sort=dso,symbol \
  --percent-limit "${perf_percent_limit}" \
  -w 8,22,120 \
  > "${prefix}.perf.report.txt"

python3 - "${prefix}.commands.json" "${prefix}.time.tsv" "${prefix}.time.md" "${prefix}.logs" <<'PY'
import csv
import json
import re
import subprocess
import sys
import time
from pathlib import Path

in_json = Path(sys.argv[1])
out_tsv = Path(sys.argv[2])
out_md = Path(sys.argv[3])
log_dir = Path(sys.argv[4])
log_dir.mkdir(parents=True, exist_ok=True)

data = json.loads(in_json.read_text(encoding="utf-8"))
commands = data.get("commands", [])

def parse_wall_seconds(value: str) -> float:
    value = value.strip()
    # Formats observed: "0:00.82", "1:23.45", "1:02:03"
    if not value:
        return 0.0
    parts = value.split(":")
    try:
        if len(parts) == 3:
            hours = int(parts[0])
            minutes = int(parts[1])
            seconds = float(parts[2])
            return hours * 3600 + minutes * 60 + seconds
        if len(parts) == 2:
            minutes = int(parts[0])
            seconds = float(parts[1])
            return minutes * 60 + seconds
        return float(value)
    except ValueError:
        return 0.0

re_user = re.compile(r"^\s*User time \(seconds\):\s*(.+)\s*$")
re_sys = re.compile(r"^\s*System time \(seconds\):\s*(.+)\s*$")
re_wall = re.compile(r"^\s*Elapsed \(wall clock\) time \(h:mm:ss or m:ss\):\s*(.+)\s*$")
re_rss = re.compile(r"^\s*Maximum resident set size \(kbytes\):\s*(\d+)\s*$")

rows = []
for entry in commands:
    target = entry["target"]
    command = entry["command"]

    time_file = log_dir / f"{target}.time.txt"
    out_file = log_dir / f"{target}.out.txt"

    with out_file.open("wb") as out_fp:
        proc = subprocess.run(
            ["/usr/bin/time", "-v", "-o", str(time_file), "bash", "-lc", command],
            stdout=out_fp,
            stderr=subprocess.STDOUT,
            check=True,
        )

    user_s = sys_s = wall_s = 0.0
    max_rss_kb = 0
    for line in time_file.read_text(encoding="utf-8", errors="replace").splitlines():
        m = re_user.match(line)
        if m:
            try:
                user_s = float(m.group(1))
            except ValueError:
                pass
            continue
        m = re_sys.match(line)
        if m:
            try:
                sys_s = float(m.group(1))
            except ValueError:
                pass
            continue
        m = re_wall.match(line)
        if m:
            wall_s = parse_wall_seconds(m.group(1))
            continue
        m = re_rss.match(line)
        if m:
            max_rss_kb = int(m.group(1))
            continue

    rows.append(
        {
            "target": target,
            "wall_s": wall_s,
            "user_s": user_s,
            "sys_s": sys_s,
            "max_rss_kb": max_rss_kb,
            "log": str(out_file),
        }
    )

rows_sorted = sorted(rows, key=lambda r: r["wall_s"], reverse=True)

with out_tsv.open("w", encoding="utf-8", newline="") as fp:
    w = csv.writer(fp, delimiter="\t")
    w.writerow(["target", "wall_s", "user_s", "sys_s", "max_rss_kb", "log"])
    for r in rows_sorted:
        w.writerow([r["target"], f"{r['wall_s']:.3f}", f"{r['user_s']:.3f}", f"{r['sys_s']:.3f}", r["max_rss_kb"], r["log"]])

total_wall = sum(r["wall_s"] for r in rows)
peak_rss = max((r["max_rss_kb"] for r in rows), default=0)

lines = []
lines.append("# Codegen per-target time / RSS\n")
lines.append(f"- Targets: {len(rows)}\n")
lines.append(f"- Total wall time (sum): {total_wall:.3f} s\n")
lines.append(f"- Peak RSS: {peak_rss} kB ({peak_rss/1024.0:.1f} MiB)\n")
lines.append("\n| Target | Wall (s) | User (s) | Sys (s) | Max RSS (MiB) |\n")
lines.append("|---|---:|---:|---:|---:|\n")
for r in rows_sorted:
    lines.append(f"| {r['target']} | {r['wall_s']:.3f} | {r['user_s']:.3f} | {r['sys_s']:.3f} | {r['max_rss_kb']/1024.0:.1f} |\n")
out_md.write_text("".join(lines), encoding="utf-8")

print(f"[profile] Wrote per-target timings -> {out_tsv}")
print(f"[profile] Wrote per-target summary  -> {out_md}")
print(f"[profile] Logs directory           -> {log_dir}")
PY

echo "[profile] Done."
echo "[profile] meta:        ${prefix}.meta.txt"
echo "[profile] commands:    ${prefix}.commands.sh"
echo "[profile] perf data:   ${prefix}.perf.data"
echo "[profile] perf report: ${prefix}.perf.report.txt"
echo "[profile] timings:     ${prefix}.time.tsv"
echo "[profile] summary:     ${prefix}.time.md"
