#!/usr/bin/env python3
"""Allure results follow the Allure 2 result model and stay linked across runs."""
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


def write_run(binary, directory):
    before = int(time.time() * 1000)
    run(binary, f"--allure-dir={directory}", *SMOKE)
    after = int(time.time() * 1000)
    results = {}
    for path in sorted(directory.glob("*-result.json")):
        result = json.loads(path.read_text(encoding="utf-8"))
        assert result["fullName"] not in results, (path, result["fullName"])
        results[result["fullName"]] = result
    return results, before, after


def main():
    binary, root = sys.argv[1], Path(sys.argv[2]).resolve()
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    inventory = {entry["name"]: entry for entry in json.loads(run(binary, "--list-json").stdout)}

    runs = [write_run(binary, root / name) for name in ("first", "second")]
    expected = {name for name, entry in inventory.items() if "death" not in entry["tags"]}
    uuids = set()
    for (results, before, after), directory in zip(runs, (root / "first", root / "second")):
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
            assert labels["suite"] == [entry["suite"]], result
            assert labels["framework"] == ["gentest"] and labels["language"] == ["cpp"], result
            assert sorted(labels.get("tag", [])) == sorted(entry["tags"]), (entry["tags"], result)
            for attachment in result.get("attachments", []):
                assert set(attachment) == {"name", "source", "type"}, attachment
                assert (directory / attachment["source"]).is_file(), attachment
        assert len(case_ids) == len(results), "each case has its own historyId"

    first, second = runs[0][0], runs[1][0]
    for name in expected:
        assert first[name]["historyId"] == second[name]["historyId"], ("history is not linked across runs", name)
        assert first[name]["uuid"] != second[name]["uuid"], name


if __name__ == "__main__":
    main()
