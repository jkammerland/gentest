"""Compiler-scoped exemptions must not hide missing runtime coverage."""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from coverage_common import compiler_family, intentional_sources, read_policy_config


class CoveragePolicyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.repo = Path(self.temporary.name).resolve()
        self.scripts = self.repo / "scripts"
        self.scripts.mkdir()
        for name in ("coverage_common.py", "coverage_hygiene.py", "coverage_report.py", "coverage_hygiene.toml"):
            shutil.copyfile(ROOT / "scripts" / name, self.scripts / name)
        self.build = self.repo / "build"
        self.build.mkdir()
        self.policy = read_policy_config(self.scripts / "coverage_hygiene.toml")

    def compiler(self, name, executable=""):
        identity = f"CMAKE_CXX_COMPILER_ID:STRING={name}\n" if name is not None else ""
        (self.build / "CMakeCache.txt").write_text(identity + f"CMAKE_CXX_COMPILER:FILEPATH={executable}\n", encoding="utf-8")

    def compiler_file(self, version, identity):
        path = self.build / "CMakeFiles" / version / "CMakeCXXCompiler.cmake"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f'set(CMAKE_CXX_COMPILER_ID "{identity}")\n', encoding="utf-8")
        return path

    def cmake_version(self, version):
        with (self.build / "CMakeCache.txt").open("a", encoding="utf-8") as cache:
            for part, value in zip(("MAJOR", "MINOR", "PATCH"), version.split("."), strict=True):
                cache.write(f"CMAKE_CACHE_{part}_VERSION:INTERNAL={value}\n")

    def test_only_inline_barrier_compilers_exempt_the_native_fallback(self):
        for family in ("Clang", "AppleClang", "GNU", "MSVC", "Unknown"):
            with self.subTest(compiler=family):
                self.compiler(family)
                paths = intentional_sources(self.policy, self.build)
                self.assertEqual("src/bench_util.cpp" in paths, family in {"Clang", "AppleClang", "GNU"})
                self.assertNotIn("src/benchmark.cpp", paths)
                self.assertIn("missing_gcda", self.policy["fail_on"].split(","))

    def test_known_cache_identity_precedes_a_misleading_compiler_path(self):
        for identity, expected in (("MSVC", ""), ("IntelLLVM", ""), ("Unknown", ""), ("", ""),
                                   ("Clang", "clang"), ("AppleClang", "clang"), ("GNU", "gcc")):
            with self.subTest(identity=identity):
                self.compiler(identity, "C:/clang-sdk/bin/cl.exe")
                self.assertEqual(compiler_family(self.build), expected)
                self.assertEqual("src/bench_util.cpp" in intentional_sources(self.policy, self.build), bool(expected))

    def test_current_cmake_compiler_file_precedes_cached_identity_and_stale_directories(self):
        self.compiler_file("3.31.8", "Clang")
        for identity, expected in (("GNU", "gcc"), ("Clang", "clang"), ("AppleClang", "clang"),
                                   ("MSVC", ""), ("IntelLLVM", "")):
            with self.subTest(identity=identity):
                self.compiler("Clang", "C:/clang-sdk/bin/clang++.exe")
                self.cmake_version("4.4.4")
                self.compiler_file("4.4.4", identity)
                self.assertEqual(compiler_family(self.build), expected)

    def test_ambiguous_missing_and_malformed_active_metadata_cannot_exempt_sources(self):
        self.compiler(None, "clang++")
        self.compiler_file("3.31.8", "Clang")
        self.assertEqual(compiler_family(self.build), "", "An unversioned cache cannot identify an active compiler file")
        self.compiler_file("4.4.3", "Clang")
        self.assertEqual(compiler_family(self.build), "")

        self.cmake_version("4.4.4")
        self.assertEqual(compiler_family(self.build), "", "A stale directory is not the active compiler")
        active = self.compiler_file("4.4.4", "Clang")
        for contents in ('set(CMAKE_CXX_COMPILER_ID)\n',
                         'set(CMAKE_CXX_COMPILER_ID "Clang")\nset(CMAKE_CXX_COMPILER_ID "GNU")\n'):
            with self.subTest(contents=contents):
                active.write_text(contents, encoding="utf-8")
                self.assertEqual(compiler_family(self.build), "")
                self.assertNotIn("src/bench_util.cpp", intentional_sources(self.policy, self.build))

    def test_path_fallback_uses_unambiguous_executable_names_only(self):
        for executable, expected in (("/sdk/bin/clang++-23", "clang"), (r"C:\sdk\clang-cl.exe", "clang"),
                                     ("/clang-sdk/bin/g++-14", "gcc"), (r"C:\clang-sdk\cl.exe", ""),
                                     ("/sdk/bin/clang-wrapper", ""), ("/sdk/bin/c++", "")):
            with self.subTest(executable=executable):
                self.compiler(None, executable)
                self.assertEqual(compiler_family(self.build), expected)

    def test_real_cmake_metadata_remains_authoritative_with_a_misleading_cache_path(self):
        project = self.repo / "compiler-project"
        project.mkdir()
        (project / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.31)\nproject(coverage_identity LANGUAGES CXX)\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/identity.txt" "${CMAKE_CXX_COMPILER_ID}")\n', encoding="utf-8")
        configured = self.repo / "configured"
        result = subprocess.run(["cmake", "-S", str(project), "-B", str(configured)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        identity = (configured / "identity.txt").read_text(encoding="utf-8")
        expected = {"Clang": "clang", "AppleClang": "clang", "GNU": "gcc"}.get(identity, "")

        # Real CMake normally stores the ID in its versioned compiler file.
        cache_path = configured / "CMakeCache.txt"
        cache = re.sub(r"(?m)^CMAKE_CXX_COMPILER:[^=]+=.*$", "CMAKE_CXX_COMPILER:FILEPATH=C:/clang-sdk/bin/cl.exe",
                       cache_path.read_text(encoding="utf-8"))
        cache_path.write_text(cache + "\nCMAKE_CXX_COMPILER_ID:STRING=MSVC\n", encoding="utf-8")
        self.assertEqual(compiler_family(configured), expected)
        self.assertEqual("src/bench_util.cpp" in intentional_sources(self.policy, configured), bool(expected))

    def test_invalid_compiler_exemption_is_rejected(self):
        for extra in ('clang = "src"', 'unknown = ["src"]', 'gcc = [7]'):
            with self.subTest(extra=extra):
                path = self.repo / "invalid.toml"
                path.write_text("[intentional_by_compiler]\n" + extra + "\n", encoding="utf-8")
                with self.assertRaises(RuntimeError):
                    read_policy_config(path)

    def hygiene_fixture(self):
        source_dir = self.repo / "src"
        source_dir.mkdir()
        commands = []
        for name in ("bench_util", "benchmark"):
            source = source_dir / f"{name}.cpp"
            source.write_text(f"void {name}() {{}}\n", encoding="utf-8")
            obj = self.build / f"{name}.cpp.o"
            obj.touch()
            commands.append({"directory": str(self.repo), "file": str(source),
                             "arguments": ["clang++", "-c", str(source), "-o", str(obj)]})
        (self.build / "compile_commands.json").write_text(json.dumps(commands), encoding="utf-8")
        covered = self.build / "benchmark.cpp.gcda"
        covered.touch()
        frontend = self.repo / "fake_gcov.py"
        frontend.write_text(textwrap.dedent("""\
            import gzip
            import json
            from pathlib import Path
            import sys
            if '--help' in sys.argv:
                print('--json-format --preserve-paths -j')
                raise SystemExit(0)
            source = Path(sys.argv[-1])
            with gzip.open('fixture.gcov.json.gz', 'wt', encoding='utf-8') as output:
                json.dump({'files': [{'file': str(source), 'lines': [{'count': 1}]}]}, output)
            """), encoding="utf-8")
        return covered, frontend

    def run_hygiene(self, frontend):
        return subprocess.run([sys.executable, str(self.scripts / "coverage_hygiene.py"),
                               "--build-dir", str(self.build), "--roots", "src",
                               "--gcov", sys.executable, str(frontend)], capture_output=True, text=True)

    def test_cli_exempts_fallback_but_still_fails_other_missing_counters(self):
        covered, frontend = self.hygiene_fixture()
        self.compiler("Clang")
        result = self.run_hygiene(frontend)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertRegex(result.stdout, r"src/bench_util\.cpp\s+intentional")
        self.assertRegex(result.stdout, r"src/benchmark\.cpp\s+ok")

        covered.unlink()
        result = self.run_hygiene(frontend)
        self.assertNotEqual(result.returncode, 0)
        self.assertRegex(result.stdout, r"src/benchmark\.cpp\s+missing_gcda")

    def test_native_compiler_does_not_inherit_the_fallback_exemption(self):
        _, frontend = self.hygiene_fixture()
        self.compiler("MSVC", "C:/clang-sdk/bin/cl.exe")
        result = self.run_hygiene(frontend)
        self.assertNotEqual(result.returncode, 0)
        self.assertRegex(result.stdout, r"src/bench_util\.cpp\s+missing_gcda")

    def test_report_cli_uses_the_same_compiler_scoped_exclusions(self):
        module = self.repo / "gcovr.py"
        module.write_text(textwrap.dedent("""\
            import json
            import os
            from pathlib import Path
            import sys
            args = sys.argv[1:]
            Path(os.environ['GCOVR_ARGS']).write_text(json.dumps(args), encoding='utf-8')
            summary = Path(args[args.index('--json-summary') + 1])
            summary.write_text(json.dumps({'line_percent': 90, 'branch_percent': 90, 'files': []}), encoding='utf-8')
            Path(args[args.index('--html-details') + 1]).write_text('<html></html>', encoding='utf-8')
            """), encoding="utf-8")
        arguments = self.repo / "args.json"
        env = os.environ | {"PYTHONPATH": str(self.repo), "GCOVR_ARGS": str(arguments)}
        for compiler in ("Clang", "MSVC"):
            with self.subTest(compiler=compiler):
                self.compiler(compiler, "C:/clang-sdk/bin/cl.exe")
                result = subprocess.run([sys.executable, str(self.scripts / "coverage_report.py"),
                                         "--build-dir", str(self.build), "--gcov", sys.executable],
                                        env=env, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                args = json.loads(arguments.read_text(encoding="utf-8"))
                exclusions = [args[index + 1] for index, value in enumerate(args) if value == "--exclude"]
                fallback = (self.repo / "src/bench_util.cpp").as_posix()
                self.assertEqual(any(re.search(pattern, fallback) for pattern in exclusions), compiler == "Clang")


if __name__ == "__main__":
    unittest.main()
