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
from coverage_common import intentional_sources, read_policy_config


class CoveragePolicyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.repo = Path(self.temporary.name)
        self.scripts = self.repo / "scripts"
        self.scripts.mkdir()
        for name in ("coverage_common.py", "coverage_hygiene.py", "coverage_report.py", "coverage_hygiene.toml"):
            shutil.copyfile(ROOT / "scripts" / name, self.scripts / name)
        self.build = self.repo / "build"
        self.build.mkdir()
        self.policy = read_policy_config(self.scripts / "coverage_hygiene.toml")

    def compiler(self, name):
        (self.build / "CMakeCache.txt").write_text(f"CMAKE_CXX_COMPILER_ID:STRING={name}\n", encoding="utf-8")

    def test_only_inline_barrier_compilers_exempt_the_native_fallback(self):
        for family in ("Clang", "AppleClang", "GNU", "MSVC", "Unknown"):
            with self.subTest(compiler=family):
                self.compiler(family)
                paths = intentional_sources(self.policy, self.build)
                self.assertEqual("src/bench_util.cpp" in paths, family in {"Clang", "AppleClang", "GNU"})
                self.assertNotIn("src/benchmark.cpp", paths)
                self.assertIn("missing_gcda", self.policy["fail_on"].split(","))

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
        self.compiler("MSVC")
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
                self.compiler(compiler)
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
