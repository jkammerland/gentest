#!/usr/bin/env python3
"""Verify that Xmake checks the project root after an absent prefix candidate."""

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
        project_args = ["-P", str(project), "-F", str(project / "xmake.lua")]
        run([xmake, "f", *project_args, "-c", "-y", "-o", str(project / "build")], project, env)
        output = run([xmake, "show", *project_args, "-t", "gentest_root_probe", "--json"], project, env)
        target = json.loads(output)
        include_dirs = {entry["value"] for entry in target["includedirs"]}
        expected = str(project / "include")
        if expected not in include_dirs:
            raise SystemExit(f"Xmake did not resolve the Gentest project root: {sorted(include_dirs)}")


if __name__ == "__main__":
    main()
