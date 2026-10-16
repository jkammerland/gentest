#!/usr/bin/env python3

from __future__ import annotations

import json
import sys
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

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

            # Creating symlinks requires an elevated token on some Windows
            # hosts. Mock the predicate so this safety contract is portable.
            with patch.object(Path, "is_symlink", return_value=True):
                with self.assertRaisesRegex(ValueError, "symlinked fixture work directory"):
                    check_configure_failure(options, "unused")


class DependencyIncludeTests(unittest.TestCase):
    def test_first_configure_uses_fmt_target_before_a_cache_file_exists(self):
        helper = Path(__file__).resolve().parents[1] / "cmake/scripts/CheckModuleFixtureCommon.cmake"
        for target, build_interface in [("fmt::fmt", False), ("fmt::fmt-header-only", False), ("fmt::fmt", True)]:
            with self.subTest(target=target, build_interface=build_interface), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                include = root / "installed fmt/include"
                include.mkdir(parents=True)
                expression = f"$<BUILD_INTERFACE:{include.as_posix()}>" if build_interface else include.as_posix()
                (root / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.31)
project(dependency_include_contract NONE)
include([===[{helper.as_posix()}]===])
add_library({target} INTERFACE IMPORTED)
set_property(TARGET {target} PROPERTY INTERFACE_INCLUDE_DIRECTORIES [===[{expression}]===])
if(EXISTS "${{CMAKE_BINARY_DIR}}/CMakeCache.txt")
  message(FATAL_ERROR "Regression must exercise the first configure")
endif()
gentest_append_public_dependency_include_args(_args "${{CMAKE_BINARY_DIR}}")
file(WRITE "${{CMAKE_BINARY_DIR}}/args.txt" "${{_args}}")
''', encoding="utf-8")
                result = subprocess.run(["cmake", "-S", str(root), "-B", str(root / "build"), "-G", "Ninja"],
                                        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertEqual((root / "build/args.txt").read_text(), "-I" + include.as_posix())


class CompileProbeTargetTests(unittest.TestCase):
    def test_compile_only_probes_forward_targets_only_to_clang(self):
        scripts = Path(__file__).resolve().parents[1] / "cmake/scripts"
        for script in ("CheckBenchUtilOptimization.cmake", "CheckVendoredIndicatorsCompile.cmake"):
            for identity, target_expected in (("GNU", False), ("Clang", True), ("AppleClang", True), ("Unknown", False)):
                for target in ("configured-target", ""):
                    with self.subTest(script=script, identity=identity, target=target), tempfile.TemporaryDirectory() as tmp:
                        root = Path(tmp) / "probe with spaces"
                        source = root / "source"
                        source.mkdir(parents=True)
                        log = root / "compiler.jsonl"
                        driver = root / "compiler.py"

                        # Isolate argument forwarding from platform compiler availability.
                        # Real optimization/header contracts are exercised by their CTests.
                        driver.write_text("""import json
from pathlib import Path
import sys
log, expected = sys.argv[1:3]
args = sys.argv[3:]
targets = [arg for arg in args if arg.startswith('--target=')]
assert targets == ([] if expected == 'none' else [expected]), targets
assert '--sysroot=configured-sysroot' in args, args
assert '-DGENTEST_COMPILE_PROBE=1' in args, args
assert Path(args[args.index('-c') + 1]).is_file(), args
Path(args[args.index('-o') + 1]).write_text('compiler fixture output')
with Path(log).open('a') as stream:
    stream.write(json.dumps(args) + '\\n')
""", encoding="utf-8")
                        if script == "CheckBenchUtilOptimization.cmake":
                            probe = source / "tests/bench_util/optimization.cpp"
                            probe.parent.mkdir(parents=True)
                            probe.write_text("// Argument-forwarding fixture.\n", encoding="utf-8")
                            checker = source / "tests/scripts/check_bench_util_assembly.py"
                            checker.parent.mkdir(parents=True)
                            checker.write_text("from pathlib import Path\nimport sys\n"
                                               "assert Path(sys.argv[1]).read_text() == 'compiler fixture output'\n", encoding="utf-8")

                        expected = "--target=" + target if target_expected and target else "none"
                        arg1 = subprocess.list2cmdline([str(driver), str(log), expected])
                        result = subprocess.run(["cmake", f"-DBUILD_ROOT={root / 'build'}", f"-DSOURCE_DIR={source}",
                            f"-DCXX_COMPILER={sys.executable}", f"-DCXX_COMPILER_ARG1={arg1}", f"-DCXX_COMPILER_ID={identity}",
                            f"-DCXX_COMPILER_TARGET={target}", "-DCXX_SYSROOT=configured-sysroot",
                            "-DCXX_FLAGS=-DGENTEST_COMPILE_PROBE=1", f"-DPYTHON={sys.executable}", "-P", str(scripts / script)],
                            capture_output=True, text=True)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        invocations = [json.loads(line) for line in log.read_text().splitlines()]
                        self.assertEqual(len(invocations), 1 if script == "CheckBenchUtilOptimization.cmake" else 2)


if __name__ == "__main__":
    unittest.main()
