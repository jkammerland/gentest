#!/usr/bin/env python3
"""Exercise owned runtime evidence through generated cases and public reporters."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

exe = Path(sys.argv[1]).resolve()
root = Path(sys.argv[2]).resolve()
allure_enabled = sys.argv[3].upper() in ("ON", "TRUE", "1")
if root.exists():
    shutil.rmtree(root)
root.mkdir(parents=True)
smoke = ["--bench-epochs=2", "--bench-warmup=0", "--bench-min-epoch-time-s=0.0001",
         "--bench-min-total-time-s=0", "--bench-max-total-time-s=0.01"]


def run(*args, rc=0):
    result = subprocess.run([str(exe), "--no-color", *args, *smoke], capture_output=True, text=True, timeout=60)
    assert result.returncode == rc, (args, result.returncode, result.stdout, result.stderr)
    return result


def bundles(directory):
    return [(p.parent, json.loads(p.read_text())) for p in sorted(directory.glob("run-*/index.json"))]


def verify_payloads(directory, index):
    bags = [index["run"]] + [s["data"] for s in index["suites"]] + [c["data"] for c in index["cases"]]
    paths = []
    for bag in bags:
        for sequence, record in enumerate(bag["records"]):
            assert record["sequence"] == sequence
            path = directory / record["path"]
            assert path.parent == directory
            paths.append(path)
            assert path.read_bytes() == (b"" if record["name"] == "empty" else b"a\x00\xff")
    assert len(paths) == len(set(paths))


output = root / "success"
junit = root / "success.xml"
extra = [f"--allure-dir={root / 'allure'}"] if allure_enabled else []
run("--filter=rt_recording/ok/*", "--repeat=2", "--shuffle", "--seed=42",
    f"--records={output}", f"--junit={junit}", *extra)
directory, index = bundles(output)[0]
assert index["schemaVersion"] == 1
assert index["errors"] == []
assert len(index["cases"]) == 8  # Three tests repeated twice, one bench, one jitter.
assert len({case["id"] for case in index["cases"]}) == 8
assert index["run"]["properties"]["run_teardown"] is True
assert index["run"]["properties"]["explicit_run"] == 7
scopes = {s["name"]: s["data"]["properties"] for s in index["suites"]}
assert scopes["rt_recording"]["suite_allocation"] is True
assert scopes["rt_recording"]["suite_teardown"] is True
assert scopes["rt_recording/ok"]["child_suite"] == "child"
for case in index["cases"]:
    props = case["data"]["properties"]
    assert props["local_setup"] is True and props["local_teardown"] is True, case
    assert case["outcome"] == "pass"
    if case["name"].endswith("/scalars"):
        assert props["text"] == "owned"
        assert props["replace"] == "last"
        assert props["bool"] is True and props["null"] is None
        assert props["min"] == -(2**63) and props["max"] == 2**64 - 1
        assert props["double"] == 1.25
        assert props["invalid_utf8"] == "\ufffd"
        assert props["nul"] == "a\0b"
        assert props['quotes"<&'] == 'Unicode: å\n"<&'
    else:
        assert "text" not in props
    if case["name"].endswith("/asynchronous"):
        assert props["before"] is True and props["after"] is True
verify_payloads(directory, index)

for case in ET.parse(junit).getroot().findall("testcase"):
    props = {p.attrib["name"]: p.attrib["value"] for p in case.findall("properties/property")}
    assert props["gentest.property.precedence"] == ("case" if case.attrib["name"].endswith("/scalars") else "suite")
    assert props["gentest.property.run_teardown"] == "true"
    assert (junit.parent / props["gentest.records"]).resolve() == directory / "index.json"

if allure_enabled:
    reports = [json.loads(p.read_text()) for p in (root / "allure").glob("*-result.json")]
    assert len(reports) == 8
    shared = []
    for report in reports:
        assert report["parameters"]
        parameters = {p["name"]: p["value"] for p in report["parameters"]}
        if report["name"].endswith("/scalars"):
            assert parameters["gentest.property.invalid_utf8"] == "\ufffd"
            assert parameters["gentest.property.nul"] == "a\0b"
        for attachment in report["attachments"]:
            path = root / "allure" / attachment["source"]
            assert path.is_file()
            if attachment["name"] == "global":
                shared.append(path)
    assert len(shared) == 8 and len(set(shared)) == 1

# Runtime failures keep earlier records and local teardown evidence.
for name, expected, rc in [("invalid", "fail", 1), ("exception", "fail", 1), ("skip", "skip", 0),
                           ("xfail", "xfail", 0), ("timed", "fail", 1)]:
    output = root / name
    result = run(f"--run=rt_recording/bad/{name}", f"--records={output}", rc=rc)
    directory, index = bundles(output)[0]
    case = index["cases"][0]
    assert case["outcome"] == expected, (name, case)
    assert case["data"]["properties"]["local_teardown"] is True
    if name == "timed":
        assert "forbidden" not in case["data"]["properties"]
        assert "timed bench/jitter" in result.stderr
    if name == "invalid":
        assert "nan" not in case["data"]["properties"]
        assert len(case["data"]["records"]) == 2
    verify_payloads(directory, index)

run("--filter=rt_recording/cancel/*", "--fail-fast", f"--records={root / 'cancel'}", rc=1)
_, cancelled = bundles(root / "cancel")[0]
waiting = next(c for c in cancelled["cases"] if c["name"].endswith("a_wait"))
assert waiting["outcome"] == "canceled"
assert waiting["data"]["properties"]["started"] is True
assert waiting["data"]["properties"]["local_teardown"] is True

# Repeated runner invocations produce fresh bundles and fresh scope state.
run("--record-twice", "--run=rt_recording/ok/separate", f"--records={root / 'twice'}")
first, second = [index for _, index in bundles(root / "twice")]
assert first["run"]["properties"]["run"] == 1
assert second["run"]["properties"]["run"] == 2
assert "first_run_only" not in second["run"]["properties"]
assert first["cases"][0]["id"] == second["cases"][0]["id"] == 0

fixture_error = run("--record-fixture-error", "--run=rt_recording/ok/scalars", f"--records={root / 'fixture-error'}", rc=1)
_, index = bundles(root / "fixture-error")[0]
assert "scope is unavailable" in fixture_error.stderr
assert index["cases"][0]["outcome"] == "blocked"
assert index["suites"][0]["data"]["records"]

# JUnit alone creates the binary sidecar; list mode does not execute hooks.
run("--run=rt_recording/ok/separate", f"--junit={root / 'alone.xml'}")
assert len(bundles(root / "alone.xml.records")) == 1
run("--list-json", f"--records={root / 'list'}")
assert not (root / "list").exists()
run("--run=rt_recording/ok/separate")  # Recording also works without exporters.
blocked = root / "not-a-directory"
blocked.write_text("keep me")
run("--run=rt_recording/ok/separate", f"--records={blocked}", rc=1)
assert blocked.read_text() == "keep me"
for args in [("--record-outside",), ("--run=rt_recording/bad/adopted", "--include-death")]:
    result = subprocess.run([str(exe), *args], capture_output=True, text=True, timeout=20)
    assert result.returncode != 0
    assert "recording" in result.stderr
print("runtime recording: scope, lifecycle, ownership, payload and export checks passed")
