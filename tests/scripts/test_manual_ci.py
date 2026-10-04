#!/usr/bin/env python3
"""Contract checks for the repository's manual CI entry points.

Actionlint validates YAML and workflow-call schemas; these checks enforce the
event and concurrency policy across the discovered workflow inventory.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"


def events_block(text):
    match = re.search(r"(?m)^(?:on|'on'|\"on\"):\n((?:[ \t].*\n|\n)+)", text)
    if not match:
        raise AssertionError("Expected a workflow event mapping")
    return match.group(1)


class ManualCiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workflows = {path.name: path.read_text(encoding="utf-8") for path in WORKFLOWS.glob("*.yml")}
        cls.reusable = {name: text for name, text in cls.workflows.items()
                        if re.search(r"(?m)^  workflow_call:", events_block(text))}

    def test_only_lint_runs_automatically_on_pull_requests(self):
        self.assertTrue(self.workflows)
        automatic = set()
        for name, text in self.workflows.items():
            with self.subTest(workflow=name):
                events = re.findall(r"(?m)^  ([a-z_]+):", events_block(text))
                if "pull_request" in events:
                    automatic.add(name)
                self.assertNotIn("pull_request_target", events)
        self.assertEqual(automatic, {"lint.yml"})

    def test_pull_request_lint_is_unconditional_for_both_checks(self):
        text = self.workflows["lint.yml"]
        self.assertIn("types: [opened, synchronize, reopened, ready_for_review]", events_block(text))
        self.assertNotRegex(events_block(text), r"(?m)^    (?:paths|paths-ignore|branches|branches-ignore):")
        jobs = dict(re.findall(r"(?m)^  ([a-z-]+):\n((?:    .*\n|\n)+)", text.split("jobs:\n", 1)[1]))
        self.assertEqual(set(jobs), {"format", "tidy"})
        for name, body in jobs.items():
            with self.subTest(job=name):
                self.assertNotRegex(body, r"(?m)^    (?:if|needs):")
        self.assertNotIn("run_lint", text)

    def test_windows_validation_uses_relwithdebinfo(self):
        text = self.workflows["cmake.yml"].split("  windows:\n", 1)[1].split("  linux:\n", 1)[0]
        self.assertEqual(text.count("-DCMAKE_BUILD_TYPE=RelWithDebInfo"), 2)
        self.assertEqual(text.count("--config RelWithDebInfo"), 2)
        self.assertIn("-C RelWithDebInfo", text)
        self.assertEqual(text.count('"-C", "RelWithDebInfo"'), 2)
        self.assertNotIn("-DCMAKE_BUILD_TYPE=Debug", text)
        self.assertNotIn("GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS", text)

    def test_windows_contract_rejects_ci_and_runtime_gate_regressions(self):
        workflow = ".github/workflows/cmake.yml"
        files = {path: (ROOT / path).read_text(encoding="utf-8") for path in (
            workflow, "tests/CMakeLists.txt", "tests/cmake/Regressions.cmake",
        )}
        mutations = (
            ("Clang configure", workflow,
             '"-DCMAKE_BUILD_TYPE=RelWithDebInfo",', '"-DCMAKE_BUILD_TYPE=Debug",'),
            ("MSVC configure", workflow,
             "-DCMAKE_BUILD_TYPE=RelWithDebInfo `", "-DCMAKE_BUILD_TYPE=Debug `"),
            ("Clang build", workflow,
             "--build --preset=${{ matrix.preset }} --config RelWithDebInfo",
             "--build --preset=${{ matrix.preset }} --config Debug"),
            ("MSVC build", workflow,
             "--build $msvcBuildDir --config RelWithDebInfo", "--build $msvcBuildDir --config Debug"),
            ("Bazel helper CTest", workflow,
             "--preset=${{ matrix.preset }} -C RelWithDebInfo", "--preset=${{ matrix.preset }} -C Debug"),
            ("MSVC CTest", workflow,
             '"--test-dir", $msvcBuildDir, "-C", "RelWithDebInfo"',
             '"--test-dir", $msvcBuildDir, "-C", "Debug"'),
            ("CI death-test skipping", workflow,
             '"-DCMAKE_BUILD_TYPE=RelWithDebInfo",',
             '"-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DGENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS=ON",'),
            ("wrong runtime configuration", "tests/CMakeLists.txt",
             'PROPERTIES DISABLED "$<CONFIG:Debug>"', 'PROPERTIES DISABLED "$<CONFIG:RelWithDebInfo>"'),
            ("legacy multi-config gate", "tests/cmake/Regressions.cmake",
             "if(WIN32 AND GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS)",
             'if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS)'),
        )
        with tempfile.TemporaryDirectory() as tmp:
            fixture = Path(tmp)
            for path, contents in files.items():
                destination = fixture / path
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_text(contents, encoding="utf-8")

            def check_contract():
                return subprocess.run([
                    "cmake", f"-DSOURCE_DIR={fixture}", "-P",
                    str(ROOT / "tests/cmake/scripts/CheckWindowsDebugDeathSkipGate.cmake"),
                ], check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

            result = check_contract()
            self.assertEqual(result.returncode, 0, result.stdout)
            for name, path, before, after in mutations:
                with self.subTest(regression=name):
                    self.assertIn(before, files[path], "the regression fixture must change the intended input")
                    (fixture / path).write_text(files[path].replace(before, after, 1), encoding="utf-8")
                    try:
                        result = check_contract()
                        self.assertNotEqual(result.returncode, 0, result.stdout)
                        self.assertIn("CMake Error", result.stdout)
                    finally:
                        (fixture / path).write_text(files[path], encoding="utf-8")

    def test_bundle_calls_every_reusable_suite_once_at_the_same_revision(self):
        calls = re.findall(r"(?m)^    uses: \./\.github/workflows/([^\s]+)$", self.workflows["ci.yml"])
        self.assertTrue(self.reusable)
        self.assertEqual(set(calls), set(self.reusable))
        self.assertEqual(len(calls), len(set(calls)))
        # Release publication must stay outside the validation bundle.
        self.assertNotIn("release.yml", calls)

    def test_each_suite_retains_a_manual_entry_point(self):
        for name, text in {"ci.yml": self.workflows["ci.yml"], **self.reusable}.items():
            with self.subTest(workflow=name):
                self.assertRegex(events_block(text), r"(?m)^  workflow_dispatch:")

    def test_master_push_starts_one_bundle(self):
        automatic = {name for name, text in self.workflows.items()
                     if re.search(r"(?m)^  push:", events_block(text))}
        self.assertEqual(automatic, {"ci.yml"})
        self.assertIn("branches: [master]", events_block(self.workflows["ci.yml"]))

    def test_routine_bundle_keeps_expensive_suites_behind_full(self):
        text = self.workflows["ci.yml"]
        self.assertIn("default: pr", events_block(text))
        self.assertIn("CI_PROFILE: ${{ inputs.profile || 'pr' }}", text)
        jobs = dict(re.findall(r"(?m)^  ([a-z-]+):\n((?:    .*\n|\n)+)", text.split("jobs:\n", 1)[1]))
        full_only = {name for name, body in jobs.items() if "profile == 'full'" in body}
        self.assertEqual(full_only, {"coverage", "cross-qemu", "buildsystems", "measured", "clang-leaks"})
        self.assertIn('*) echo "Unknown CI profile" >&2; exit 1', text)

    def test_reused_suites_do_not_cancel_each_other_or_the_caller(self):
        groups = []
        for name, text in {"ci.yml": self.workflows["ci.yml"], **self.reusable}.items():
            with self.subTest(workflow=name):
                match = re.search(r"(?m)^  group: ([a-z-]+)-\$\{\{ github.ref \}\}$", text)
                self.assertIsNotNone(match, "use a distinct fixed suite prefix; github.workflow belongs to the caller")
                groups.append(match.group(1))
        self.assertEqual(len(groups), len(set(groups)))

    def test_manual_pr_profile_controls_bash_and_powershell_runners(self):
        text = self.workflows["cmake.yml"]
        self.assertIn("GENTEST_CI_PROFILE: ${{ inputs.profile || 'pr' }}", text)
        self.assertIn('if [ "${GENTEST_CI_PROFILE}" = "pr" ]', text)
        self.assertIn('if ($env:GENTEST_CI_PROFILE -eq "pr")', text)
        self.assertNotIn('if [ "${GITHUB_EVENT_NAME}" = "pull_request" ]', text)
        self.assertNotIn('if ($env:GITHUB_EVENT_NAME -eq "pull_request")', text)

    def test_measured_base_is_resolved_once_and_passed_to_the_comparison(self):
        text = self.workflows["measured_reports.yml"]
        self.assertIn("CI_BASE_REF: ${{ inputs.base_ref || 'master' }}", text)
        self.assertIn("base_sha: ${{ steps.base.outputs.sha }}", text)
        self.assertIn("CI_BASE_SHA: ${{ needs.plan.outputs.base_sha }}", text)
        self.assertNotIn("github.event.pull_request.base", text)
        self.assertIn('git fetch --no-tags -- origin "${CI_BASE_REF}"', text)
        self.assertIn('git worktree add --detach "${GENTEST_MEASURED_BASE_SOURCE}" "${CI_BASE_SHA}"', text)
        self.assertIn("base_ref: ${{ inputs.base_ref || github.event.before }}", self.workflows["ci.yml"])


if __name__ == "__main__":
    unittest.main()
