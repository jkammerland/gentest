#!/usr/bin/env python3
"""Allure results follow the Allure 2 result model, stay linked across runs, and two runs
sharing one directory keep every file of both. Results are named after their program, so
same-named tests of two programs in one report are not merged into one test."""
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

from target_command import target_command

UUID = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")
CASE_ID = re.compile(r"^[0-9a-f]{16}$")
RESULT_FILE = re.compile(r"^([0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})-[0-9]+-result\.json$")
STATUSES = {"passed", "failed", "broken", "skipped"}
SMOKE = ["--bench-epochs=2", "--bench-warmup=0", "--bench-min-epoch-time-s=0.0001",
         "--bench-min-total-time-s=0", "--bench-max-total-time-s=0.01"]


def run(binary, *args):
    result = subprocess.run(target_command(binary, "--no-color", *args), capture_output=True, text=True,
                            encoding="utf-8", timeout=120)
    assert result.returncode == 0, (args, result.returncode, result.stdout, result.stderr)
    return result


def labels_of(result):
    labels = {}
    for label in result["labels"]:
        assert set(label) == {"name", "value"}, label
        labels.setdefault(label["name"], []).append(label["value"])
    return labels


def snapshot(directory):
    return {path.name: path.read_bytes() for path in directory.iterdir() if path.is_file()}


def write_run(binary, directory):
    """Runs the binary into a directory that may hold earlier runs; returns this run's results."""
    existing = snapshot(directory) if directory.exists() else {}
    before = int(time.time() * 1000)
    run(binary, f"--allure-dir={directory}", *SMOKE)
    after = int(time.time() * 1000)
    now = snapshot(directory)
    for name, contents in existing.items():
        assert now.get(name) == contents, ("an earlier run's file was changed or removed", name)
    added = sorted(set(now) - set(existing))
    matches = [RESULT_FILE.match(name) for name in added if name.endswith("-result.json")]
    assert matches and all(matches), ("result files are named <token>-<n>-result.json", added)
    tokens = {match.group(1) for match in matches}
    assert len(tokens) == 1, ("one report token per run", added)
    token = tokens.pop()
    assert all(name.startswith(token + "-") for name in added), ("every new file carries the run's token", added)
    results = {}
    for name in added:
        if not name.endswith("-result.json"):
            continue
        assert len(now[name]) > 1, name
        result = json.loads(now[name].decode("utf-8"))
        assert result["name"] not in results, (name, result["name"])
        results[result["name"]] = result
    return results, before, after, set(added)


def main():
    binary, root = sys.argv[1], Path(sys.argv[2]).resolve()
    program = Path(binary).stem
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    inventory = {entry["name"]: entry for entry in json.loads(run(binary, "--list-json").stdout)}

    # Two runs share one directory, as several executables or shards do.
    directory = root / "shared"
    runs = [write_run(binary, directory) for _ in range(2)]
    assert not runs[0][3] & runs[1][3], "the runs share no file name"
    # A renamed copy is another program with the same test names, e.g. a second build variant.
    copy = Path(binary).with_name(f"{program}_variant{Path(binary).suffix}")
    shutil.copy2(binary, copy)
    try:
        variant = write_run(str(copy), directory)
    finally:
        copy.unlink()
    expected = {name for name, entry in inventory.items() if "death" not in entry["tags"]}
    uuids = set()
    for results, before, after, added in runs:
        assert set(results) == expected, (sorted(results), sorted(expected))
        case_ids = set()
        for name, result in results.items():
            entry = inventory[name]
            assert UUID.match(result["uuid"]), result
            assert result["uuid"] not in uuids, ("uuid reused", result)
            uuids.add(result["uuid"])
            assert CASE_ID.match(result["historyId"]), result
            assert result["testCaseId"] == result["historyId"], result
            case_ids.add(result["historyId"])
            assert result["name"] == name, result
            assert result["fullName"] == f"{program}:{name}", result
            assert result["stage"] == "finished", result
            assert result["status"] in STATUSES, result
            if entry["skipped"]:
                assert result["status"] == "skipped", result
                if entry["skipReason"]:
                    assert result["statusDetails"]["message"] == entry["skipReason"], result
            assert isinstance(result["start"], int) and isinstance(result["stop"], int), result
            # Millisecond clocks: allow one tick of slack on each side of the run.
            assert before - 1 <= result["start"] <= result["stop"] <= after + 1, (before, after, result)
            labels = labels_of(result)
            assert labels["parentSuite"] == [program], result
            assert labels["suite"] == [entry["suite"]], result
            assert labels["framework"] == ["gentest"] and labels["language"] == ["cpp"], result
            assert sorted(labels.get("tag", [])) == sorted(entry["tags"]), (entry["tags"], result)
            assert sorted(labels.get("requirement", [])) == sorted(entry["requirements"]), (entry["requirements"], result)
            for attachment in result.get("attachments", []):
                assert set(attachment) == {"name", "source", "type"}, attachment
                assert attachment["source"] in added, ("attachments come from the same run", attachment)
        assert len(case_ids) == len(results), "each case has its own historyId"

    first, second = runs[0][0], runs[1][0]
    for name in expected:
        assert first[name]["historyId"] == second[name]["historyId"], ("history is not linked across runs", name)
        assert first[name]["uuid"] != second[name]["uuid"], name

    variant_results = variant[0]
    assert set(variant_results) == expected, sorted(variant_results)
    for name in expected:
        result = variant_results[name]
        assert result["fullName"] == f"{program}_variant:{name}", result
        assert labels_of(result)["parentSuite"] == [f"{program}_variant"], result
        assert result["historyId"] != first[name]["historyId"], ("another program's test is not a retry", name)


if __name__ == "__main__":
    main()
