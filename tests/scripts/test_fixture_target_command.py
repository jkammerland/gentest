#!/usr/bin/env python3
"""Run recorded fixture targets with their own executor, including native fallbacks."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def atom(value):
    return "[=[" + str(value).replace(";", r"\;") + "]=]"


class FixtureTargetCommandTests(unittest.TestCase):
    def test_target_metadata_drives_direct_and_environment_wrapped_children(self):
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake)
        env = os.environ.copy()
        env["GENTEST_HELPER_BUILD_PARALLEL_LEVEL"] = "1"
        configuration = "RelWithDebInfo" if os.name == "nt" else "Debug"

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, build = root / "source", root / "build"
            source.mkdir()
            record = root / "executor.jsonl"
            wrapper = root / "emulator with spaces.py"
            marker = 'one ; " \\ argument'
            wrapper.write_text(f'''import json, pathlib, subprocess, sys
with pathlib.Path({str(record)!r}).open("a", encoding="utf-8") as stream:
    stream.write(json.dumps(sys.argv[1:]) + "\\n")
assert sys.argv[1] == {marker!r}, sys.argv
raise SystemExit(subprocess.call(sys.argv[2:]))
''')
            (source / "main.cpp").write_text('''#include <cstdio>
int main(int argc, char** argv) {
    if (argc != 2) return 90;
    std::puts(argv[1]);
    return argv[1][0] == 'f' ? 7 : 0;
}
''')
            (source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.20)
project(fixture_target_command LANGUAGES CXX)
add_subdirectory(cross)
add_subdirectory(native)
''')
            prefix = " ".join(map(atom, (sys.executable, wrapper, marker)))
            # The native directory deliberately inherits an emulator property as well.
            for name, cross in (("cross", "TRUE"), ("native", "FALSE")):
                directory = source / name
                directory.mkdir()
                (directory / "CMakeLists.txt").write_text(f'''set(CMAKE_CROSSCOMPILING {cross})
set(CMAKE_CROSSCOMPILING_EMULATOR {prefix})
add_executable({name}_target ../main.cpp)
''')
            sentinel = root / "existing include.cmake"
            sentinel.write_text('set(EXISTING_TOP_LEVEL_INCLUDE_WORKED ON CACHE BOOL "" FORCE)\n')
            configure = root / "configure.cmake"
            configure.write_text(f'''include("{ROOT.as_posix()}/tests/cmake/scripts/CheckRunOrFail.cmake")
gentest_check_run_or_fail(COMMAND "${{CMAKE_COMMAND}}" -S {atom(source)} -B {atom(build)} {atom('-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=' + str(sentinel))} "-DCMAKE_BUILD_TYPE={configuration}")
gentest_check_run_or_fail(COMMAND "${{CMAKE_COMMAND}}" --build {atom(build)} --config {configuration} --parallel 1)
''')
            result = subprocess.run([cmake, "-P", str(configure)], capture_output=True, text=True, env=env)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("EXISTING_TOP_LEVEL_INCLUDE_WORKED:BOOL=ON", (build / "CMakeCache.txt").read_text())
            exe = ".exe" if os.name == "nt" else ""
            targets = {}
            for name in ("cross", "native"):
                candidates = list((build / name).rglob(name + "_target" + exe))
                self.assertEqual(len(candidates), 1, candidates)
                targets[name] = candidates[0]
            run = root / "run.cmake"
            cross_argument = 'passed ; " \\ cross'
            run.write_text(f'''include("{ROOT.as_posix()}/tests/cmake/scripts/CheckRunOrFail.cmake")
gentest_check_run_or_fail(COMMAND {atom(targets['cross'])} {atom(cross_argument)} OUTPUT_VARIABLE cross_output)
gentest_check_run_or_fail(COMMAND "${{CMAKE_COMMAND}}" -E env "NESTED_CONTROL=1" {atom(targets['native'])} "passed native" OUTPUT_VARIABLE native_output)
message(STATUS "${{cross_output}}${{native_output}}")
''')
            result = subprocess.run([cmake, "-P", str(run)], capture_output=True, text=True, env=env)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(cross_argument, result.stdout)
            self.assertIn("passed native", result.stdout)
            self.assertEqual([json.loads(line) for line in record.read_text().splitlines()],
                             [[marker, targets["cross"].as_posix(), cross_argument]])

            # A nonzero guest result must remain a failure through the environment wrapper.
            run.write_text(f'''include("{ROOT.as_posix()}/tests/cmake/scripts/CheckRunOrFail.cmake")
gentest_check_run_or_fail(COMMAND "${{CMAKE_COMMAND}}" -E env "NESTED_CONTROL=1" {atom(targets['cross'])} "failed cross")
''')
            result = subprocess.run([cmake, "-P", str(run)], capture_output=True, text=True, env=env)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Command failed (7)", result.stderr)
            self.assertEqual(json.loads(record.read_text().splitlines()[-1]),
                             [marker, targets["cross"].as_posix(), "failed cross"])


if __name__ == "__main__":
    unittest.main()
