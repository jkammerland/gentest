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
    def test_full_native_lanes_and_windows_skip(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, {}, clear=True):
            output = Path(temp)
            linux = validator.make_plan("Linux", ROOT, output, output / "base", 2)
            self.assertEqual(len(linux), 16)
            stages = {s.name: s for s in linux}
            self.assertIn("--preset=alusan-system", stages["asan-ubsan"].commands[0].args)
            self.assertIn("--preset=tsan-system", stages["tsan"].commands[0].args)
            self.assertIn("--parallel", stages["coverage"].commands[2].args)
            self.assertTrue(stages["recording"].missing)
            self.assertIn("gentest_benchmarks_tests", stages["measured"].commands[1].args)
            for stage in linux[:6]:
                self.assertNotIn("--label-regex", stage.commands[2].args)
                self.assertIn("--no-tests=error", stage.commands[2].args)
            windows = validator.make_plan("Windows", ROOT, output, output / "base", 2)
            self.assertEqual([s.name for s in windows], ["clang-debug", "clang-release", "msvc-debug", "msvc-release"])
            for stage in windows:
                self.assertEqual("-DGENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS=ON" in stage.commands[0].args, stage.name.endswith("-debug"))
            mac = validator.make_plan("Darwin", ROOT, output, output / "base", 2)
            self.assertEqual([s.name for s in mac], ["appleclang-debug", "appleclang-release", "llvm-debug", "llvm-release"])

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
