from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import validate_local as validator
import validation_suites
from llvm_tools import LLVM_MAJOR, TOOL_OVERRIDES, Toolchain
from llvm_tool_fixture import fake_toolchain
from validation_suites import Command, suite_commands

COMMIT = "1" * 40
BASE = "2" * 40


class ReportsTests(unittest.TestCase):
    def reports(self, parent):
        directories = []
        for host in ("Linux", "Windows", "Darwin"):
            directory = parent / host
            directory.mkdir()
            log = directory / "evidence.log"
            log.write_text("Successful test output\n")
            evidence = validator.artifact(log, directory)
            report = {"schema": 1, "commit": COMMIT, "base_commit": BASE, "host": host,
                      "validator": validator.validator_hashes(), "source_verified": True, "status": "PASS", "errors": [],
                      "stages": [{"name": name, "status": "PASS", "commands": [{"exit_code": 0, "log": evidence}],
                                  "evidence": [evidence]} for name in validator.required_stages(host)]}
            validator.save_report(directory, report)
            directories.append(directory)
        return directories

    def test_collect_is_offline_and_requires_all_three_hosts(self):
        with tempfile.TemporaryDirectory() as temp:
            dirs = self.reports(Path(temp))
            with patch("subprocess.Popen", side_effect=AssertionError("collect must not spawn processes")), \
                    patch("socket.create_connection", side_effect=AssertionError("collect must not connect")):
                self.assertEqual(validator.collect(COMMIT, dirs)["status"], "PASS")
                self.assertEqual(validator.collect(COMMIT, dirs[:2])["status"], "INCOMPLETE")
                self.assertEqual(validator.collect(COMMIT, dirs + dirs[:1])["status"], "INCOMPLETE")
                self.assertEqual(validator.collect(COMMIT, [])["status"], "INCOMPLETE")

    def test_collect_rejects_wrong_revision_base_missing_stages_and_failures(self):
        mutations = [lambda r: r.update(commit=BASE), lambda r: r.update(base_commit=COMMIT),
                     lambda r: r.update(source_verified=False), lambda r: r["stages"].pop(),
                     lambda r: r["stages"][0].update(status="INCOMPLETE"),
                     lambda r: r["stages"][0].update(status="FAIL"),
                     lambda r: r["stages"][0].update(commands=[]),
                     lambda r: r["stages"][0].update(evidence=[]),
                     lambda r: r["stages"][0]["commands"][0].update(exit_code=1),
                     lambda r: r.update(validator={}), lambda r: r.update(schema=999)]
        for mutate in mutations:
            with self.subTest(mutation=mutate), tempfile.TemporaryDirectory() as temp:
                dirs = self.reports(Path(temp))
                report = json.loads((dirs[0] / "report.json").read_text())
                mutate(report)
                validator.save_report(dirs[0], report)
                self.assertEqual(validator.collect(COMMIT, dirs)["status"], "INCOMPLETE")

    def test_collect_rejects_tampered_missing_and_escaping_evidence(self):
        for operation in ("modify", "remove", "escape"):
            with self.subTest(operation=operation), tempfile.TemporaryDirectory() as temp:
                dirs = self.reports(Path(temp))
                log = dirs[0] / "evidence.log"
                if operation == "modify":
                    log.write_text("Changed output\n")
                elif operation == "remove":
                    log.unlink()
                else:
                    report = json.loads((dirs[0] / "report.json").read_text())
                    report["stages"][0]["evidence"][0]["path"] = "../Windows/evidence.log"
                    validator.save_report(dirs[0], report)
                self.assertEqual(validator.collect(COMMIT, dirs)["status"], "INCOMPLETE")

    def test_junit_requires_real_executed_tests_and_retains_skip_reason(self):
        with tempfile.TemporaryDirectory() as temp:
            xml = Path(temp) / "tests.xml"
            xml.write_text('<testsuite><testcase name="ok"/><testcase name="limited"><skipped message="unsupported toolchain"/></testcase></testsuite>')
            summary = validator.junit_summary(xml)
            self.assertEqual(summary, {"tests": 2, "failures": [], "skips": [{"test": "limited", "reason": "unsupported toolchain"}]})
            xml.write_text('<testsuite><testcase name="broken"><failure/></testcase></testsuite>')
            self.assertEqual(validator.junit_summary(xml)["failures"], ["broken"])
            xml.write_text('<testsuite><testcase name="disabled" status="disabled"><system-out>Disabled</system-out></testcase>'
                           '<testcase name="notrun" status="notrun"><skipped message="unsupported toolchain"/></testcase></testsuite>')
            self.assertEqual(validator.junit_summary(xml)["skips"], [
                {"test": "disabled", "reason": "CTest disabled test"},
                {"test": "notrun", "reason": "unsupported toolchain"}])
            xml.write_text('<testsuite tests="500"/>')
            with self.assertRaises(ValueError):
                validator.junit_summary(xml)


class ExecutionTests(unittest.TestCase):
    def test_stage_failure_stops_dependents_but_independent_stage_runs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "evidence").mkdir()
            fail = validator.Stage("fail", [Command([sys.executable, "-c", "raise SystemExit(7)"], root),
                                            Command([sys.executable, "-c", "raise AssertionError('must not run')"], root)])
            passed = validator.Stage("independent", [Command([sys.executable, "-c", "print('validated')"], root)])
            failed = validator.run_stage(fail, root, 10, {}, {})
            self.assertEqual(failed["status"], "FAIL")
            self.assertEqual(len(failed["commands"]), 1)
            prior = {"fail": failed}
            blocked = validator.Stage("blocked", passed.commands, dependencies=["fail"])
            self.assertEqual(validator.run_stage(blocked, root, 10, prior, {})["status"], "INCOMPLETE")
            result = validator.run_stage(passed, root, 10, prior, {})
            self.assertEqual(result["status"], "PASS")
            log = root / result["evidence"][0]["path"]
            self.assertIn("validated", log.read_text())

    def test_missing_capability_and_timeout_do_not_pass(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "evidence").mkdir()
            missing = validator.Stage("missing", [], missing="serializer dependencies unavailable")
            self.assertEqual(validator.run_stage(missing, root, 10, {}, {})["status"], "INCOMPLETE")
            command = Command([sys.executable, "-c", "import time; time.sleep(60)"], root)
            result = validator.execute(command, root / "timeout.log", 1)
            self.assertIsNone(result["exit_code"])
            self.assertIn("exceeded", result["error"])

    def test_all_disabled_junit_cases_cannot_pass_a_stage(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "evidence").mkdir()
            xml = root / "evidence" / "disabled.xml"
            write_xml = "from pathlib import Path; Path(__import__('sys').argv[-1]).write_text(" \
                "'<testsuite><testcase name=\"disabled\" status=\"disabled\"/></testsuite>')"
            command = Command([sys.executable, "-c", write_xml, "--output-junit", str(xml)], root)
            result = validator.run_stage(validator.Stage("disabled", [command]), root, 10, {}, {})
            self.assertEqual(result["status"], "FAIL")
            self.assertEqual(result["reason"], "JUnit contains failures or no executed tests")

    def test_source_must_be_exact_and_tracked_clean_but_untracked_work_is_preserved(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            def git(*args):
                return validator.git(root, *args)
            git("init", "--initial-branch=master")
            git("config", "user.name", "Validator Test")
            git("config", "user.email", "validator@example.invalid")
            git("config", "commit.gpgsign", "false")
            tracked = root / "tracked.txt"
            tracked.write_text("committed\n")
            git("add", "tracked.txt")
            git("commit", "-m", "test")
            head = git("rev-parse", "HEAD")
            (root / "private-untracked.txt").write_text("keep this")
            self.assertEqual(validator.verify_source(root, head), head)
            tracked.write_text("uncommitted\n")
            with self.assertRaisesRegex(ValueError, "tracked modifications"):
                validator.verify_source(root, head)
            with self.assertRaises(subprocess.CalledProcessError):
                validator.verify_source(root, COMMIT)
            self.assertEqual((root / "private-untracked.txt").read_text(), "keep this")

    def test_private_output_never_overwrites_or_lives_inside_checkout(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with self.assertRaises(ValueError):
                validator.private_directory(root / "reports", root)
            with self.assertRaises(FileExistsError):
                validator.private_directory(root)


class PlanTests(unittest.TestCase):
    def setUp(self):
        prefix = Path("/selected-llvm")
        self.selection = patch.object(validator, "select_toolchain", return_value=Toolchain(
            {name: str(prefix / "bin" / name) for name in TOOL_OVERRIDES},
            str(prefix / "lib/cmake/llvm"), str(prefix / "lib/cmake/clang")))
        self.selection.start()
        self.addCleanup(self.selection.stop)

    def commands_without_preflight(self, stage):
        return [command.args for command in stage.commands if "check-toolchain" not in command.args[:3]]

    @unittest.skipIf(os.name == "nt", "Fake executables use POSIX shell scripts")
    def test_explicit_llvm_prefix_keeps_compilers_libraries_and_coverage_together(self):
        self.selection.stop()
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            prefix = fake_toolchain(output / "llvm", LLVM_MAJOR)

            with patch.dict(os.environ, {"GENTEST_LLVM_PREFIX": str(prefix)}, clear=True):
                stages = validator.make_plan("Linux", ROOT, output, output / "base", 2)

            for stage in stages:
                for command in stage.commands:
                    self.assertEqual(command.env["LLVM_DIR"], str(prefix / "lib/cmake/llvm"))
                    self.assertEqual(command.env["Clang_DIR"], str(prefix / "lib/cmake/clang"))
                    self.assertTrue(command.env["PATH"].startswith(str(prefix / "bin") + os.pathsep))
            coverage = next(stage for stage in stages if stage.name == "coverage")
            gcov_commands = [command.args for command in coverage.commands if "--gcov" in command.args]
            self.assertEqual(len(gcov_commands), 2)
            for command in gcov_commands:
                self.assertEqual(command[command.index("--gcov") + 1], str(prefix / "bin/llvm-cov"))

    @unittest.skipIf(os.name == "nt", "Fake executables use POSIX shell scripts")
    def test_stale_explicit_prefix_with_valid_path_never_executes_configure(self):
        self.selection.stop()
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            stale = fake_toolchain(output / "stale", 21)
            valid = fake_toolchain(output / "valid", LLVM_MAJOR)
            with patch.dict(os.environ, {"PATH": str(valid / "bin"), "GENTEST_LLVM_PREFIX": str(stale)}, clear=True):
                stages = validator.make_plan("Linux", ROOT, output, output / "base", 2)
                with patch.object(validator, "execute", side_effect=AssertionError("configure must not execute")) as execute, \
                        patch.object(validator.shutil, "which", side_effect=lambda name: name):
                    for stage in stages:
                        self.assertIn("Expected LLVM", stage.missing)
                        result = validator.run_stage(stage, output, 10, {}, {})
                        self.assertEqual(result["status"], "INCOMPLETE")
                    execute.assert_not_called()

    def test_full_native_lanes_and_windows_configuration(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, {}, clear=True):
            output = Path(temp)
            linux = validator.make_plan("Linux", ROOT, output, output / "base", 2)
            self.assertEqual(len(linux), 16)
            stages = {s.name: s for s in linux}
            for stage in linux:
                self.assertEqual(stage.commands[0].args[1:3], [str(ROOT / "scripts/llvm_tools.py"), "check-toolchain"])
            self.assertIn("--preset=alusan-system", self.commands_without_preflight(stages["asan-ubsan"])[0])
            self.assertIn("--preset=tsan-system", self.commands_without_preflight(stages["tsan"])[0])
            self.assertIn("--parallel", self.commands_without_preflight(stages["coverage"])[2])
            self.assertTrue(stages["recording"].missing)
            self.assertIn("gentest_benchmarks_tests", self.commands_without_preflight(stages["measured"])[1])
            for stage in linux[:6]:
                self.assertNotIn("--label-regex", self.commands_without_preflight(stage)[2])
                self.assertIn("--no-tests=error", self.commands_without_preflight(stage)[2])
            windows = validator.make_plan("Windows", ROOT, output, output / "base", 2)
            self.assertEqual([s.name for s in windows], ["clang-relwithdebinfo", "msvc-relwithdebinfo"])
            for stage in windows:
                self.assertEqual(stage.commands[0].args[1:3], [str(ROOT / "scripts/llvm_tools.py"), "check-toolchain"])
                configure, build, test = self.commands_without_preflight(stage)
                self.assertIn("-DCMAKE_BUILD_TYPE=RelWithDebInfo", configure)
                self.assertEqual(build[build.index("--config") + 1], "RelWithDebInfo")
                self.assertEqual(test[test.index("-C") + 1], "RelWithDebInfo")
            mac = validator.make_plan("Darwin", ROOT, output, output / "base", 2)
            self.assertEqual([s.name for s in mac], ["appleclang-debug", "appleclang-release", "llvm-debug", "llvm-release"])

    @unittest.skipIf(os.name == "nt", "Fake executables use POSIX shell scripts")
    def test_missing_analysis_tools_do_not_block_build_lanes(self):
        self.selection.stop()
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            prefix = fake_toolchain(root / "llvm", LLVM_MAJOR)
            for tool in ("clang-format", "clang-tidy", "llvm-cov"):
                (prefix / "bin" / tool).unlink()
            with patch.dict(os.environ, {"GENTEST_LLVM_PREFIX": str(prefix)}, clear=True):
                stages = {stage.name: stage for stage in validator.make_plan("Linux", ROOT, root, root / "base", 2)}
                windows = validator.make_plan("Windows", ROOT, root, root / "base", 2)
            self.assertFalse(stages["clang-debug"].missing)
            self.assertTrue(stages["lint"].missing)
            self.assertTrue(stages["coverage"].missing)
            self.assertTrue(all(not stage.missing for stage in windows))

    def test_suite_target_compiler_is_independent_of_host_llvm(self):
        with tempfile.TemporaryDirectory() as temp, \
                patch.object(sys, "argv", ["validation_suites.py", "bazel", "--work", temp, "--cc", "gcc", "--cxx", "g++"]), \
                patch.object(validation_suites, "select_toolchain", return_value=Toolchain(
                    {"clang": "/llvm/bin/clang", "clang++": "/llvm/bin/clang++", "clang-scan-deps": "/llvm/bin/clang-scan-deps"},
                    host_clang="/llvm/bin/clang++")) as select, \
                patch.object(validation_suites, "suite_commands", return_value=[]) as commands:
            self.assertEqual(validation_suites.main(), 0)
        self.assertEqual(commands.call_args.kwargs["cc"], "gcc")
        self.assertEqual(commands.call_args.kwargs["cxx"], "g++")
        self.assertEqual(commands.call_args.kwargs["host_clang"], "/llvm/bin/clang++")
        self.assertNotIn("cc", select.call_args.kwargs)

    def test_backend_commands_keep_gcc_targets_and_host_scanning_separate(self):
        with tempfile.TemporaryDirectory() as temp:
            commands = suite_commands("bazel", ROOT, Path(temp), cc="gcc", cxx="g++",
                                      host_clang="/host/clang++-23", host_scan_deps="/host/clang-scan-deps-23")
            for command in commands:
                self.assertEqual(command.env["CC"], "gcc")
                self.assertEqual(command.env["CXX"], "g++")
                self.assertEqual(command.env["GENTEST_BAZEL_LOCAL_CLANG"], "/host/clang++-23")
                self.assertEqual(command.env["GENTEST_BAZEL_LOCAL_CLANG_SCAN_DEPS"], "/host/clang-scan-deps-23")
                if command.args[0] == "bazel":
                    self.assertIn("--repo_env=GENTEST_BAZEL_LOCAL_CLANG_SCAN_DEPS", command.args)
            commands = suite_commands("xmake", ROOT, Path(temp), cc="gcc", cxx="g++", codegen=Path("/host/codegen"))
            self.assertIn("--cc=gcc", commands[0].args)
            self.assertIn("--cxx=g++", commands[0].args)

    def test_boost_json_include_override_keeps_the_optional_writer_enabled(self):
        with tempfile.TemporaryDirectory() as temp, \
                patch.dict(os.environ, {"GENTEST_BOOST_JSON_INCLUDE_DIR": "/installed/boost/include"}):
            output = Path(temp)
            stages = validator.make_plan("Windows", ROOT, output, output / "base", 2)
            for stage in stages:
                configure = self.commands_without_preflight(stage)[0]
                self.assertIn("-DGENTEST_USE_BOOST_JSON=ON", configure)
                self.assertIn("-DGENTEST_BOOST_JSON_INCLUDE_DIR=/installed/boost/include", configure)

    def test_ci_uses_the_shared_acceptance_commands(self):
        recording = (ROOT / ".github/workflows/recording.yml").read_text()
        buildsystems = (ROOT / ".github/workflows/buildsystems_linux.yml").read_text()
        self.assertIn("scripts/validation_suites.py recording", recording)
        for suite in ("bazel", "meson", "xmake"):
            self.assertIn("scripts/validation_suites.py " + suite, buildsystems)
        with tempfile.TemporaryDirectory() as temp:
            commands = suite_commands("recording", ROOT, Path(temp), serializers=temp)
            self.assertTrue(any("--expect-cbor" in c.args and "--expect-json" in c.args for c in commands))
            self.assertTrue(any("gentest_record_json_noexceptions" in c.args for c in commands))
            for command in commands:
                if "--output-junit" in command.args:
                    self.assertIn("--no-tests=error", command.args)


if __name__ == "__main__":
    unittest.main()
