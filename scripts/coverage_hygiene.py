#!/usr/bin/env python3
"""Coverage hygiene for codegen and testlib translation units.

The script inspects ``compile_commands.json`` and compares each tracked source
file under the selected roots against coverage data collected under the build
directory. It reports:

- missing coverage artifacts (no ``.gcda``)
- stale coverage artifacts (``gcov`` stamp mismatch)
- translated units with no executable lines
- zero-hit units

The output is intended for CI gating and localized review of dead/unused code.
"""

from __future__ import annotations

import argparse
import gzip
import json
import re
import shlex
import shutil
import subprocess
import tempfile
import tomllib
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Set, Tuple


GcovStatus = str
CoverageRecord = Tuple[Path, str, Optional[float], Optional[int], Optional[str], Optional[str]]
HeaderCoverageRecord = Tuple[GcovStatus, Optional[float], Optional[int], Optional[str], Optional[str]]

DEFAULT_POLICY = {
    "roots": ["src", "tools/src"],
    "exclude_prefix": [],
    "intentional": [
        "src/gentest_anchor.cpp",
        "tools/src/tooling_support.cpp",
        "src/runtime_context.cpp",
        "tools/src/terminfo_shim.cpp",
    ],
    "no_exec": [],
    "fail_on": "missing_obj,missing_gcda,stamp_mismatch,no_match,gcov_error",
    "warn_on": "zero_hits",
}
DEFAULT_POLICY_PATH = Path(__file__).with_suffix(".toml")

# Non-fatal gcov statuses that should be superseded by a usable candidate, often seen when a source is compiled by
# multiple targets and one coverage artifact is stale.
SKIP_FOR_BEST_EFFORT = {"stamp_mismatch", "no_match", "gcov_error"}
PREFERRED_ORDER: Dict[GcovStatus, int] = {
    "ok": 0,
    "no_executable_lines": 1,
    "no_exec": 1,
    "zero_hits": 2,
    "missing_gcda": 3,
    "stamp_mismatch": 4,
    "no_match": 5,
    "gcov_error": 6,
}
TRANSLATION_UNIT_SUFFIXES = (".cpp",)
IMPLEMENTATION_HEADER_SUFFIXES = (".h", ".hh", ".hpp", ".hxx", ".ipp", ".inl")


def _read_policy_config(config_path: Path) -> Dict[str, Any]:
    policy = dict(DEFAULT_POLICY)
    if not config_path.exists():
        return policy

    data = tomllib.loads(config_path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise RuntimeError(f"coverage policy config at '{config_path}' must be a TOML table")

    for key in ("roots", "exclude_prefix", "intentional", "no_exec"):
        value = data.get(key, policy[key])
        if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
            raise RuntimeError(f"coverage policy key '{key}' in '{config_path}' must be a list of strings")
        policy[key] = list(value)

    for key in ("fail_on", "warn_on"):
        value = data.get(key, policy[key])
        if not isinstance(value, str):
            raise RuntimeError(f"coverage policy key '{key}' in '{config_path}' must be a string")
        policy[key] = value

    return policy


def _to_abs_paths(project_root: Path, values: Sequence[str]) -> List[Path]:
    paths: List[Path] = []
    for value in values:
        path = Path(value)
        if not path.is_absolute():
            path = project_root / path
        paths.append(path.resolve(strict=False))
    return paths


def _load_compile_commands(build_dir: Path) -> List[Dict[str, Any]]:
    compdb_path = build_dir / "compile_commands.json"
    if not compdb_path.exists():
        raise FileNotFoundError(
            f"compile_commands.json not found at '{compdb_path}'. "
            "Reconfigure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON."
        )
    return json.loads(compdb_path.read_text(encoding="utf-8"))


def _parse_object_from_command_tokens(
    directory: str,
    tokens: Sequence[str],
    source: Path,
) -> Optional[Path]:
    idx = 0
    while idx < len(tokens):
        tok = tokens[idx]
        if tok == "-o" and idx + 1 < len(tokens):
            obj = Path(tokens[idx + 1])
            return (Path(directory) / obj).resolve()
        if tok.startswith("-o") and len(tok) > 2:
            return (Path(directory) / tok[2:]).resolve()
        idx += 1
    # Last-resort fallback: the object name usually matches the source base.
    fallback = Path(directory) / "CMakeFiles" / source.parent.name / (source.name + ".o")
    if fallback.exists():
        return fallback.resolve()
    return None


def _source_roots_map(
    compdb: Sequence[Dict[str, Any]],
    source_roots: Sequence[Path],
) -> Dict[Path, List[Path]]:
    mapping: Dict[Path, List[Path]] = {}
    for entry in compdb:
        src = Path(entry.get("file", "")).resolve()
        if not src.is_file():
            continue
        if not any(src == root or src.is_relative_to(root) for root in source_roots):
            continue
        raw_command = entry.get("command")
        if isinstance(raw_command, str):
            tokens = shlex.split(raw_command)
        else:
            raw_arguments = entry.get("arguments")
            if not isinstance(raw_arguments, list):
                continue
            tokens = [str(token) for token in raw_arguments]
            if not tokens:
                continue
        obj = _parse_object_from_command_tokens(entry.get("directory", ""), tokens, src)
        if obj is None:
            continue
        items = mapping.setdefault(src, [])
        if obj not in items:
            items.append(obj)
    return mapping


def _find_gcda_files(obj_file: Path, source: Path) -> List[Path]:
    obj_dir = obj_file.parent
    candidates = [
        obj_file.with_suffix(".gcda"),
        obj_dir / source.with_suffix(source.suffix + ".gcda").name,
        obj_dir / (source.name + ".gcda"),
        obj_dir / (source.stem + ".gcda"),
    ]
    found: List[Path] = []
    seen = set()
    for candidate in candidates:
        key = candidate.as_posix()
        if key in seen:
            continue
        seen.add(key)
        if candidate.exists():
            found.append(candidate)
    if not found:
        pattern = source.stem + "*.gcda"
        found.extend(sorted(obj_file.parent.glob(pattern)))
    # Prefer freshest artifact if wrappers generated multiple variants.
    if found:
        return sorted(found, key=lambda p: p.stat().st_mtime, reverse=True)
    return []


def _source_match_rank(source: Path, candidate: str) -> int:
    if not candidate:
        return 0
    source_parts = source.resolve().parts
    candidate_parts = Path(candidate).resolve().parts
    if source.name != Path(candidate).name:
        return 0
    rank = 0
    for s, c in zip(reversed(source_parts), reversed(candidate_parts)):
        if s != c:
            break
        rank += 1
    return rank


def _output_file_matches_source(source: Path, gcov_file: Optional[str]) -> bool:
    if gcov_file is None:
        return False
    rank = _source_match_rank(source, gcov_file)
    return rank >= 1


def _status_from_gcov_output(output: str, source: Path) -> Tuple[str, Optional[float], Optional[int], Optional[str]]:
    status = "unmatched"
    pct = None
    total = None
    last_file = None

    if "stamp mismatch with notes file" in output:
        return "stamp_mismatch", None, None, None
    if "cannot open data file" in output:
        return "missing_gcda", None, None, None
    if "assuming not executed" in output:
        status = "missing_gcda"

    for line in output.splitlines():
        line = line.strip()
        mfile = re.match(r"^File ['\"]([^'\"]+)['\"]$", line)
        if mfile:
            last_file = mfile.group(1)
            continue
        if line == "No executable lines":
            if _output_file_matches_source(source, last_file):
                return "no_executable_lines", None, 0, last_file
            continue
        mexec = re.match(r"^Lines executed:\s*([0-9]+\.[0-9]+)% of\s*([0-9]+)$", line)
        if mexec and _output_file_matches_source(source, last_file):
            pct = float(mexec.group(1))
            total = int(mexec.group(2))
            return "ok" if pct > 0 else "zero_hits", pct, total, last_file

    if status != "unmatched":
        return status, None, None, last_file
    return "no_match", None, None, last_file


def _parse_gcov_json(tmpdir: Path, source: Path) -> Optional[Tuple[float, int, str]]:
    payload = sorted(tmpdir.glob("*.gcov.json.gz"), key=lambda p: p.stat().st_mtime, reverse=True)
    if not payload:
        return None

    matches: List[Tuple[int, int, Dict[str, Any]]] = []
    for candidate_json in payload:
        with gzip.open(candidate_json, "rt", encoding="utf-8") as fh:
            data = json.load(fh)

        for entry in data.get("files", []):
            file_name = entry.get("file", "")
            rank = _source_match_rank(source, file_name)
            if rank:
                matches.append((rank, candidate_json.stat().st_mtime, entry))

    if not matches:
        return None

    # Prefer the highest-path specificity (for absolute path hits) and then latest artifact.
    best = max(matches, key=lambda match: (match[0], match[1]))
    entry = best[2]
    entry_file = entry.get("file", "")
    lines = [ln for ln in entry.get("lines", []) if isinstance(ln, dict) and "count" in ln]
    if not lines:
        return 0.0, 0, entry_file
    total_exec_lines = len(lines)
    hit = sum(1 for ln in lines if ln.get("count", 0) > 0)
    pct = (hit * 100.0) / total_exec_lines if total_exec_lines else 0.0
    return pct, total_exec_lines, entry_file


def _coverage_status_from_json_entry(entry: Dict[str, Any]) -> Tuple[GcovStatus, float, int]:
    lines = [ln for ln in entry.get("lines", []) if isinstance(ln, dict) and "count" in ln]
    if not lines:
        return "no_executable_lines", 0.0, 0

    total_exec_lines = len(lines)
    hit = sum(1 for ln in lines if ln.get("count", 0) > 0)
    pct = (hit * 100.0) / total_exec_lines if total_exec_lines else 0.0
    return ("ok" if pct > 0 else "zero_hits"), pct, total_exec_lines


def _is_under_any_root(source: Path, roots: Sequence[Path]) -> bool:
    return any(source == root or source.is_relative_to(root) for root in roots)


def _is_excluded(source: Path, exclude: Sequence[Path]) -> bool:
    return any(source == item or source.is_relative_to(item) for item in exclude)


def _collect_sources(
    roots: Sequence[Path],
    exclude_prefix: Sequence[str],
    suffixes: Sequence[str] = TRANSLATION_UNIT_SUFFIXES,
) -> List[Path]:
    exclude = [Path(prefix).resolve() for prefix in exclude_prefix]
    suffix_set = {suffix.lower() for suffix in suffixes}
    out: List[Path] = []
    for root in roots:
        if not root.exists():
            continue
        for src in root.rglob("*"):
            if not src.is_file():
                continue
            if src.suffix.lower() not in suffix_set:
                continue
            resolved = src.resolve()
            if _is_excluded(resolved, exclude):
                continue
            out.append(resolved)
    out.sort()
    return out


def _is_expected(source: Path, roots: Sequence[Path]) -> bool:
    if not roots:
        return False
    source_norm = source.resolve()
    for item in roots:
        if source_norm == item or source_norm.is_relative_to(item):
            return True
    return False


def _parse_status_set(value: str) -> Set[GcovStatus]:
    return {item.strip() for item in value.split(",") if item.strip()}


def _parse_gcov_command(spec: Sequence[str] | str) -> List[str]:
    if isinstance(spec, str):
        return [token for token in shlex.split(spec) if token]
    out: List[str] = []
    for token in spec:
        if not isinstance(token, str):
            continue
        chunks = shlex.split(token)
        if chunks:
            out.extend(chunks)
    return out


def _probe_gcov_support(gcov_cmd: Sequence[str], env: Optional[Dict[str, str]] = None) -> Set[str]:
    try:
        proc = subprocess.run(
            list(gcov_cmd) + ["--help"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
            env=env,
        )
    except OSError:
        return set()
    output = proc.stdout.lower()
    supports = set()
    for flag in ("--json-format", "--preserve-paths", "-j"):
        if f" {flag}" in output:
            supports.add(flag)
    return supports


def _gcov_invocation_args(
    source: Path,
    gcov_cmd: Sequence[str],
    gcda: Path,
    gcov_support: Set[str],
    gcov_args: Sequence[str],
) -> List[str]:
    args = list(gcov_cmd) + list(gcov_args)
    if "-j" in gcov_support:
        args.append("-j")
    if "--json-format" in gcov_support:
        args.append("--json-format")
    if "--preserve-paths" in gcov_support:
        args.append("--preserve-paths")
    args.extend(("-o", str(gcda), str(source)))
    return args


def _run_gcov(
    source: Path,
    gcov_cmd: Sequence[str],
    gcda: Path,
    gcov_support: Set[str],
    gcov_args: Sequence[str],
) -> Tuple[GcovStatus, Optional[float], Optional[int], Optional[str]]:
    args = _gcov_invocation_args(source, gcov_cmd, gcda, gcov_support, gcov_args)

    with tempfile.TemporaryDirectory(prefix="gcov_") as tmp:
        tmpdir = Path(tmp)
        proc = subprocess.run(
            args,
            cwd=tmpdir,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )

        parsed_json = _parse_gcov_json(tmpdir, source)
        if parsed_json:
            pct_j, total_j, _ = parsed_json
            status = "ok" if pct_j > 0 else ("no_executable_lines" if total_j == 0 else "zero_hits")
            return status, pct_j, total_j, str(gcda)

        status, pct, total, _ = _status_from_gcov_output(proc.stdout, source)
        if status == "no_match" and proc.returncode != 0:
            status = "gcov_error"
        return status, pct, total, str(gcda)


def _header_from_gcov_file(file_name: str, source_roots: Sequence[Path], exclude: Sequence[Path]) -> Optional[Path]:
    if not file_name:
        return None
    source = Path(file_name)
    if not source.is_absolute():
        return None
    source = source.resolve(strict=False)
    if source.suffix.lower() not in IMPLEMENTATION_HEADER_SUFFIXES:
        return None
    if not _is_under_any_root(source, source_roots):
        return None
    if _is_excluded(source, exclude):
        return None
    return source


def _is_better_header_coverage(
    candidate: HeaderCoverageRecord,
    current: Optional[HeaderCoverageRecord],
) -> bool:
    if current is None:
        return True

    candidate_priority = PREFERRED_ORDER.get(candidate[0], 99)
    current_priority = PREFERRED_ORDER.get(current[0], 99)
    if candidate_priority != current_priority:
        return candidate_priority < current_priority

    candidate_pct = candidate[1] if candidate[1] is not None else -1.0
    current_pct = current[1] if current[1] is not None else -1.0
    if candidate_pct != current_pct:
        return candidate_pct > current_pct

    candidate_lines = candidate[2] if candidate[2] is not None else -1
    current_lines = current[2] if current[2] is not None else -1
    return candidate_lines > current_lines


def _store_header_coverage(
    records: Dict[Path, HeaderCoverageRecord],
    header: Path,
    record: HeaderCoverageRecord,
) -> None:
    if _is_better_header_coverage(record, records.get(header)):
        records[header] = record


def _parse_gcov_json_header_records(
    tmpdir: Path,
    source_roots: Sequence[Path],
    exclude: Sequence[Path],
    gcda: Path,
    obj_file: Path,
) -> Dict[Path, HeaderCoverageRecord]:
    records: Dict[Path, HeaderCoverageRecord] = {}
    for candidate_json in tmpdir.glob("*.gcov.json.gz"):
        with gzip.open(candidate_json, "rt", encoding="utf-8") as fh:
            data = json.load(fh)

        for entry in data.get("files", []):
            header = _header_from_gcov_file(str(entry.get("file", "")), source_roots, exclude)
            if header is not None:
                status, pct, lines = _coverage_status_from_json_entry(entry)
                _store_header_coverage(records, header, (status, pct, lines, str(gcda), str(obj_file)))
    return records


def _parse_gcov_text_header_records(
    output: str,
    source_roots: Sequence[Path],
    exclude: Sequence[Path],
    gcda: Path,
    obj_file: Path,
) -> Dict[Path, HeaderCoverageRecord]:
    records: Dict[Path, HeaderCoverageRecord] = {}
    last_header: Optional[Path] = None
    for line in output.splitlines():
        stripped = line.strip()
        match = re.match(r"^File ['\"]([^'\"]+)['\"]$", stripped)
        if match:
            last_header = _header_from_gcov_file(match.group(1), source_roots, exclude)
            continue

        if last_header is None:
            continue
        if stripped == "No executable lines":
            _store_header_coverage(records, last_header, ("no_executable_lines", 0.0, 0, str(gcda), str(obj_file)))
            last_header = None
            continue

        mexec = re.match(r"^Lines executed:\s*([0-9]+\.[0-9]+)% of\s*([0-9]+)$", stripped)
        if mexec:
            pct = float(mexec.group(1))
            lines = int(mexec.group(2))
            status = "ok" if pct > 0 else "zero_hits"
            _store_header_coverage(records, last_header, (status, pct, lines, str(gcda), str(obj_file)))
            last_header = None
    return records


def _discover_header_records(
    compile_map: Dict[Path, List[Path]],
    source_roots: Sequence[Path],
    exclude_prefix: Sequence[str],
    gcov_cmd: Sequence[str],
    gcov_support: Set[str],
    gcov_args: Sequence[str],
) -> Dict[Path, HeaderCoverageRecord]:
    exclude = [Path(prefix).resolve() for prefix in exclude_prefix]
    header_records: Dict[Path, HeaderCoverageRecord] = {}
    scanned: Set[Tuple[Path, Path, Path]] = set()

    for source, obj_files in compile_map.items():
        for obj_file in obj_files:
            for gcda in _find_gcda_files(obj_file, source):
                scan_key = (source, obj_file, gcda)
                if scan_key in scanned:
                    continue
                scanned.add(scan_key)

                args = _gcov_invocation_args(source, gcov_cmd, gcda, gcov_support, gcov_args)
                with tempfile.TemporaryDirectory(prefix="gcov_headers_") as tmp:
                    tmpdir = Path(tmp)
                    proc = subprocess.run(
                        args,
                        cwd=tmpdir,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                        text=True,
                        check=False,
                    )
                    records = _parse_gcov_json_header_records(tmpdir, source_roots, exclude, gcda, obj_file)
                    if not records:
                        records = _parse_gcov_text_header_records(proc.stdout, source_roots, exclude, gcda, obj_file)

                for header, record in records.items():
                    _store_header_coverage(header_records, header, record)

    return header_records


def _scan_unit(
    source: Path,
    obj_files: Sequence[Path],
    gcov_cmd: Sequence[str],
    gcov_support: Set[str],
    gcov_args: Sequence[str],
) -> Tuple[GcovStatus, Optional[float], Optional[int], Optional[str], Optional[str]]:
    candidates: List[Tuple[float, Path, Path]] = []
    for obj_file in obj_files:
        for gcda in _find_gcda_files(obj_file, source):
            candidates.append((gcda.stat().st_mtime, obj_file, gcda))

    if not candidates:
        return "missing_gcda", None, None, None, None

    candidates.sort(reverse=True, key=lambda entry: entry[0])
    best_status: Optional[GcovStatus] = None
    best_pct: Optional[float] = None
    best_lines: Optional[int] = None
    best_gcda: Optional[str] = None
    best_obj: Optional[str] = None
    best_skipped_status: Optional[GcovStatus] = None
    best_skipped_pct: Optional[float] = None
    best_skipped_lines: Optional[int] = None
    best_skipped_gcda: Optional[str] = None
    best_skipped_obj: Optional[str] = None

    for _, obj_file, gcda in candidates:
        status, pct, lines, gcda_str = _run_gcov(source, gcov_cmd, gcda, gcov_support, gcov_args)
        if status == "ok":
            return status, pct, lines, gcda_str, str(obj_file)

        priority = PREFERRED_ORDER.get(status, 99)

        # Keep scanning to avoid stale or artifacted gcda files from other target builds,
        # but remember the best skipped status in case every candidate ends up in this bucket.
        if status in SKIP_FOR_BEST_EFFORT:
            if best_skipped_status is None or priority < PREFERRED_ORDER.get(best_skipped_status, 99):
                best_skipped_status = status
                best_skipped_pct = pct
                best_skipped_lines = lines
                best_skipped_gcda = gcda_str
                best_skipped_obj = str(obj_file)
            continue

        if best_status is None:
            best_status = status
            best_pct = pct
            best_lines = lines
            best_gcda = gcda_str
            best_obj = str(obj_file)
        else:
            if priority < PREFERRED_ORDER.get(best_status, 99):
                best_status = status
                best_pct = pct
                best_lines = lines
                best_gcda = gcda_str
                best_obj = str(obj_file)

    if best_status is not None:
        return best_status, best_pct, best_lines, best_gcda, best_obj
    if best_skipped_status is not None:
        return best_skipped_status, best_skipped_pct, best_skipped_lines, best_skipped_gcda, best_skipped_obj
    return "missing_gcda", None, None, None, None


def _print_report(
    group: str,
    entries: Sequence[CoverageRecord],
    actionable_statuses: Set[GcovStatus],
    project_root: Path,
) -> None:
    print(f"\n[{group}]")
    print(f"{'file':42} {'status':24} {'pct':>8} {'lines':>8} {'gcda':<60} {'obj'}")
    print("-" * 150)
    actionable = 0
    status_counts: Dict[GcovStatus, int] = {}
    for source, status, pct, lines, gcda, _ in entries:
        status_counts[status] = status_counts.get(status, 0) + 1
        source_label = str(source)
        if source.is_relative_to(project_root):
            source_label = source.relative_to(project_root).as_posix()
        else:
            source_label = source.as_posix()
        pct_s = "-" if pct is None else f"{pct:6.2f}%"
        lines_s = "-" if lines is None else str(lines)
        gcda_label = gcda or "-"
        obj = "-"
        if _:
            obj = str(_)
        print(f"{source_label:42} {status:24} {pct_s:>8} {lines_s:>8} {gcda_label:60} {obj}")
        if status in actionable_statuses:
            actionable += 1
    print("Status counts:")
    for status in sorted(status_counts):
        print(f"  {status}: {status_counts[status]}")
    if actionable:
        action_label = ", ".join(sorted(actionable_statuses))
        print(f"Actionable statuses ({action_label}): {actionable}")


def main() -> int:
    config_parser = argparse.ArgumentParser(add_help=False)
    config_parser.add_argument(
        "--config",
        default=str(DEFAULT_POLICY_PATH),
        help="Path to the coverage hygiene policy TOML file.",
    )
    config_args, remaining = config_parser.parse_known_args()
    policy = _read_policy_config(Path(config_args.config))

    ap = argparse.ArgumentParser(parents=[config_parser])
    ap.add_argument("--build-dir", default="build/coverage", help="Build directory containing compile_commands.json and *.gcda")
    ap.add_argument("--roots", nargs="*", default=policy["roots"], help="Relative source roots to inspect")
    ap.add_argument("--exclude-prefix", nargs="*", default=policy["exclude_prefix"], help="Path prefixes to skip")
    ap.add_argument(
        "--intentional",
        nargs="*",
        default=policy["intentional"],
        help="Path prefixes considered intentional placeholders (not coverage-failing).",
    )
    ap.add_argument(
        "--no-exec",
        nargs="*",
        default=policy["no_exec"],
        help="Path prefixes expected to have no executable lines.",
    )
    ap.add_argument("--gcov", nargs="+", default=["gcov"], help="gcov-compatible command line")
    ap.add_argument(
        "--gcov-args",
        nargs="*",
        default=[],
        help="Additional args appended to the gcov command.",
    )
    ap.add_argument(
        "--fail-on",
        default=policy["fail_on"],
        help="Comma-separated statuses that fail the run.",
    )
    ap.add_argument(
        "--warn-on",
        default=policy["warn_on"],
        help="Comma-separated statuses to treat as actionable in CI reports.",
    )
    ap.add_argument(
        "--ignore-statuses",
        default="",
        help="Comma-separated statuses to suppress as failures.",
    )
    args = ap.parse_args(remaining)

    project_root = Path(__file__).resolve().parents[1]
    build_dir = project_root / args.build_dir
    source_roots = _to_abs_paths(project_root, args.roots)
    intentional_roots = _to_abs_paths(project_root, args.intentional)
    no_exec_roots = _to_abs_paths(project_root, args.no_exec)
    fail_set = _parse_status_set(args.fail_on)
    ignore_set = _parse_status_set(args.ignore_statuses)
    fail_set = fail_set - ignore_set
    warn_set = _parse_status_set(args.warn_on)

    gcov_cmd = _parse_gcov_command(args.gcov)
    if not gcov_cmd:
        raise RuntimeError(f"gcov command not recognized: {args.gcov}")
    gcov_binary = shutil.which(gcov_cmd[0])
    if not gcov_binary:
        raise RuntimeError(f"gcov not found: {gcov_cmd[0]}")
    gcov_cmd[0] = str(gcov_binary)
    gcov_support = _probe_gcov_support(gcov_cmd)
    if not gcov_support:
        # Stay conservative when help probing is inconclusive. Text-mode gcov
        # parsing still works, while assuming unsupported flags can turn healthy
        # coverage runs into environment-specific gcov_error failures.
        gcov_support = set()

    compdb = _load_compile_commands(build_dir)
    compile_map = _source_roots_map(compdb, source_roots)
    sources = _collect_sources(source_roots, args.exclude_prefix)
    header_records = _discover_header_records(
        compile_map,
        source_roots,
        args.exclude_prefix,
        gcov_cmd,
        gcov_support,
        args.gcov_args,
    )
    for header, record in header_records.items():
        obj_path = Path(record[4]).resolve(strict=False) if record[4] else None
        obj_files = [obj_path] if obj_path is not None else []
        if header not in compile_map:
            compile_map[header] = obj_files
        else:
            for obj_file in obj_files:
                if obj_file not in compile_map[header]:
                    compile_map[header].append(obj_file)
    sources = sorted(set(sources).union(header_records.keys()))

    groups: Dict[str, List[CoverageRecord]] = {
        "codegen": [],
        "testlib": [],
        "other": [],
    }
    failures: List[Tuple[Path, str]] = []

    for source in sources:
        obj_files = compile_map.get(source, [])
        if source in header_records:
            status, pct, lines, gcda, obj_file = header_records[source]
            if _is_expected(source, intentional_roots):
                status = "intentional"
            elif _is_expected(source, no_exec_roots) and status == "no_executable_lines":
                status = "no_exec"
            record = (source, status, pct, lines, gcda, obj_file)
        elif not obj_files:
            record = (
                source,
                "intentional" if _is_expected(source, intentional_roots) else "missing_obj",
                None,
                None,
                None,
                None,
            )
        else:
            status, pct, lines, gcda, obj_file = _scan_unit(
                source,
                obj_files,
                gcov_cmd,
                gcov_support,
                args.gcov_args,
            )
            if _is_expected(source, intentional_roots):
                status = "intentional"
            elif _is_expected(source, no_exec_roots) and status == "no_executable_lines":
                status = "no_exec"
            record = (source, status, pct, lines, gcda, str(obj_file) if obj_file else None)

        if source.is_relative_to(project_root / "src"):
            group = "testlib"
        elif source.is_relative_to(project_root / "tools" / "src"):
            group = "codegen"
        else:
            group = "other"

        groups[group].append(record)
        if record[1] in fail_set:
            failures.append((record[0], record[1]))

    for group_name in ("codegen", "testlib", "other"):
        entries = groups[group_name]
        if entries:
            _print_report(group_name, entries, warn_set, project_root)

    if failures:
        print("\nActionable failures:")
        for source, status in failures:
            print(f"  - {source}: {status}")

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
