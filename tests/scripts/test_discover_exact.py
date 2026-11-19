"""Check exact case identity and verdicts through generated CTest registrations."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from target_command import target_command


def main():
    binary, cmake, ctest, generator = sys.argv[1:]
    repo = Path(__file__).resolve().parents[2]
    executor = ";".join(arg.replace(";", r"\;") for arg in target_command(binary)[:-1])
    env = os.environ.copy()
    env.pop("GENTEST_PROBE_HIDE_DECODE", None)

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        report = root / "ctest.xml"
        subprocess.run([cmake, "-S", str(repo / "tests/cmake/discover_exact"), "-B", directory, "-G", generator,
                        f"-DPROBE={binary}", f"-DEXECUTOR={executor}",
                        f"-DDISCOVER_HELPER={repo / 'cmake/gentest/DiscoverTests.cmake'}"], check=True, env=env)
        result = subprocess.run([ctest, "--test-dir", directory, "-C", "RelWithDebInfo", "--output-junit", str(report)],
                                capture_output=True, text=True, timeout=30, env=env)
        assert result.returncode == 8, result
        cases = {case.attrib["name"]: case for case in ET.parse(report).iter("testcase")}

        control = cases["control/decode"]
        assert control.find("failure") is not None and "entered decode body" in control.findtext("system-out"), result
        for prefix in ("include", "exclude-tag", "exclude-name"):
            case = cases[f"{prefix}/decode"]
            output = case.findtext("system-out")
            assert case.find("skipped") is not None and "[ SKIP ] decode" in output, (prefix, output)
            assert "entered " not in output, output

        stale = cases["stale/decode"]
        output = stale.findtext("system-out")
        assert stale.find("failure") is not None and "Case not found: decode" in output, output
        assert "entered " not in output, output

        # Extra arguments cannot redirect an entry or turn it into a successful preview.
        for prefix, diagnostic in (("conflicting", "duplicate --run"), ("listing", "require execution mode")):
            case = cases[f"{prefix}/decode"]
            output = case.findtext("system-out")
            assert case.find("failure") is not None and diagnostic in output, output
            assert "entered " not in output, output

    # The skip diagnostic leaves the requested machine report on stdout.
    result = subprocess.run(target_command(binary, "--gentest-run-exact=decode", "--tag=fast", "--skip-return-code=77",
                                           "--report-format=json"), capture_output=True, text=True, timeout=30, env=env)
    assert result.returncode == 77 and "[ SKIP ] decode" in result.stderr, result
    tables = json.loads(result.stdout)["tables"]
    assert tables and all(table["rows"] == [] for table in tables), tables

    # Interactive selection still resolves the same missing name as a unique suffix.
    result = subprocess.run(target_command(binary, "--run=decode"), capture_output=True, text=True, timeout=30,
                            env={**env, "GENTEST_PROBE_HIDE_DECODE": "1"})
    assert result.returncode == 0 and "entered fallback/decode body" in result.stdout, result


if __name__ == "__main__":
    main()
