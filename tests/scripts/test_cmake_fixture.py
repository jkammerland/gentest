#!/usr/bin/env python3

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

from cmake_fixture import ConfigureOptions, build_configure_argv, check_configure_failure


class CMakeFixtureTests(unittest.TestCase):
    def make_options(self, root: Path, **overrides: object) -> ConfigureOptions:
        values: dict[str, object] = {
            "cmake_command": (sys.executable,),
            "source_dir": root / "fixture with spaces",
            "build_root": root / "build root",
            "generator": "Visual Studio 17 2022",
        }
        values.update(overrides)
        return ConfigureOptions(**values)  # type: ignore[arg-type]

    def write_fake_cmake(self, root: Path, body: str) -> Path:
        script = root / "fake cmake.py"
        script.write_text(f"{body}\n", encoding="utf-8")
        return script

    def fake_cmake_command(self, script: Path) -> tuple[str, ...]:
        return (sys.executable, str(script))

    def test_build_configure_argv_preserves_platform_paths_and_semicolons(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            options = self.make_options(
                root,
                generator_platform="x64",
                generator_toolset="ClangCL",
                toolchain_file=r"C:\Program Files\toolchain.cmake",
                make_program=r"C:\Program Files\Ninja\ninja.exe",
                c_compiler="clang;wrapped",
                cxx_compiler=r"C:\LLVM 21\bin\clang++.exe",
                llvm_dir=r"C:\LLVM 21\lib\cmake\llvm",
                clang_dir=r"C:\LLVM 21\lib\cmake\clang",
                build_type="Debug",
            )

            command = build_configure_argv(options)

            self.assertEqual(command[command.index("-A") + 1], "x64")
            self.assertEqual(command[command.index("-T") + 1], "ClangCL")
            self.assertIn(r"-DCMAKE_TOOLCHAIN_FILE=C:\Program Files\toolchain.cmake", command)
            self.assertIn("-DCMAKE_C_COMPILER=clang;wrapped", command)
            self.assertIn(r"-DCMAKE_CXX_COMPILER=C:\LLVM 21\bin\clang++.exe", command)

    def test_command_prefix_preserves_interpreter_and_script_path(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            script = self.write_fake_cmake(root, "raise SystemExit(1)")
            command = build_configure_argv(
                self.make_options(root, cmake_command=self.fake_cmake_command(script))
            )

        self.assertEqual(command[:2], [sys.executable, str(script)])

    def test_empty_optional_values_are_omitted(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            command = build_configure_argv(self.make_options(Path(temporary)))

        self.assertNotIn("-A", command)
        self.assertNotIn("-T", command)
        self.assertFalse(any(argument.startswith("-D") for argument in command))

    def test_accepts_nonzero_exit_and_whitespace_normalized_diagnostic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fake_cmake = self.write_fake_cmake(
                root, "import sys\nprint('expected\\tmessage', file=sys.stderr)\nraise SystemExit(7)"
            )
            options = self.make_options(root, cmake_command=self.fake_cmake_command(fake_cmake))

            check_configure_failure(options, "expected\nmessage")

    def test_rejects_successful_configure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fake_cmake = self.write_fake_cmake(root, "print('configured')")

            with self.assertRaisesRegex(RuntimeError, "exit code was 0"):
                check_configure_failure(
                    self.make_options(root, cmake_command=self.fake_cmake_command(fake_cmake)),
                    "configured",
                )

    def test_reports_stdout_and_stderr_when_diagnostic_is_missing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fake_cmake = self.write_fake_cmake(
                root,
                "import sys\nprint('stdout marker')\nprint('stderr marker', file=sys.stderr)\nraise SystemExit(2)",
            )

            with self.assertRaisesRegex(RuntimeError, "stdout marker[\\s\\S]*stderr marker"):
                check_configure_failure(
                    self.make_options(root, cmake_command=self.fake_cmake_command(fake_cmake)),
                    "missing",
                )

    def test_removes_stale_work_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fake_cmake = self.write_fake_cmake(
                root, "import sys\nprint('expected failure')\nraise SystemExit(1)"
            )
            options = self.make_options(root, cmake_command=self.fake_cmake_command(fake_cmake))
            options.work_dir.mkdir(parents=True)
            stale = options.work_dir / "stale.txt"
            stale.write_text("stale", encoding="utf-8")

            check_configure_failure(options, "expected failure")

            self.assertFalse(stale.exists())

    def test_refuses_symlinked_work_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            options = self.make_options(root)
            target = root / "outside"
            target.mkdir()
            options.build_root.mkdir()
            options.work_dir.symlink_to(target, target_is_directory=True)

            with self.assertRaisesRegex(ValueError, "symlinked fixture work directory"):
                check_configure_failure(options, "unused")


if __name__ == "__main__":
    unittest.main()
