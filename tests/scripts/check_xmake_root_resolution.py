#!/usr/bin/env python3
"""Verify Xmake root fallback and actionable invalid-root diagnostics."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def run(command: list[str], project: Path, env: dict[str, str]) -> str:
    result = subprocess.run(command, cwd=project, env=env, capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        raise SystemExit(f"{' '.join(command)} failed:\n{result.stdout}\n{result.stderr}")
    return result.stdout


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: check_xmake_root_resolution.py <xmake> <gentest-source>")

    xmake = sys.argv[1]
    helper = Path(sys.argv[2]).resolve() / "xmake" / "gentest.lua"
    with tempfile.TemporaryDirectory(prefix="gentest-xmake-root-") as temporary:
        project = Path(temporary)
        header = project / "include" / "gentest" / "runner.h"
        header.parent.mkdir(parents=True)
        header.write_text("// Root resolution probe.\n", encoding="utf-8")
        (project / "cases.cpp").write_text("// Configuration only.\n", encoding="utf-8")
        (project / "xmake.lua").write_text(
            f"""set_project("gentest_root_probe")
set_languages("cxx20")
includes({json.dumps(str(helper), ensure_ascii=False)})
gentest_configure({{
    project_root = {json.dumps(str(project), ensure_ascii=False)},
    gentest_root = os.getenv("GENTEST_TEST_ROOT_OVERRIDE"),
    helper_root = "/tmp/gentest-xmake-shallow",
    incdirs = {{"include"}},
    gentest_common_defines = {{}},
    gentest_common_cxxflags = {{}},
}})
target("gentest_root_probe")
    set_kind("phony")
    gentest_attach_codegen({{
        name = "gentest_root_probe",
        kind = "textual",
        source = "cases.cpp",
        output_dir = "gen",
    }})
""",
            encoding="utf-8",
        )

        env = os.environ.copy()
        env["XMAKE_GLOBALDIR"] = str(project / "xmake-global")
        env.pop("GENTEST_TEST_ROOT_OVERRIDE", None)
        # The fallback under test requires a missing host-source candidate.
        # OLDPWD can otherwise name a valid checkout and bypass that branch.
        env.pop("OLDPWD", None)
        project_args = ["-P", str(project), "-F", str(project / "xmake.lua")]
        run([xmake, "f", *project_args, "-c", "-y", "-o", str(project / "build")], project, env)
        output = run([xmake, "show", *project_args, "-t", "gentest_root_probe", "--json"], project, env)
        target = json.loads(output)
        include_dirs = {entry["value"] for entry in target["includedirs"]}
        expected = str(project / "include")
        if expected not in include_dirs:
            raise SystemExit(f"Xmake did not resolve the Gentest project root: {sorted(include_dirs)}")

        # An explicit invalid prefix must fail even though the project fallback
        # is valid, and the sandbox must preserve Gentest's actionable message.
        invalid_root = project / "invalid-prefix"
        invalid_root.mkdir()
        invalid_env = env.copy()
        invalid_env["GENTEST_TEST_ROOT_OVERRIDE"] = str(invalid_root)
        result = subprocess.run(
            [xmake, "f", *project_args, "-c", "-y", "-o", str(project / "invalid-build")],
            cwd=project,
            env=invalid_env,
            capture_output=True,
            text=True,
            timeout=60,
        )
        if result.returncode == 0:
            raise SystemExit("Xmake accepted an invalid explicit gentest_root")

        diagnostic = "gentest xmake: gentest_configure `gentest_root` must point at a gentest source tree or installed prefix"
        output = result.stdout + result.stderr
        if diagnostic not in output or "attempt to call a nil value" in output:
            raise SystemExit(f"Xmake lost the invalid gentest_root diagnostic:\n{output}")


if __name__ == "__main__":
    main()
