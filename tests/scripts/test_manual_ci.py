#!/usr/bin/env python3
"""Event, revision, permission, and completion policy for the discovered CI inventory."""

import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"


def events_block(text):
    match = re.search(r"(?m)^(?:on|'on'|\"on\"):\n((?:[ \t].*\n|\n)+)", text)
    if not match:
        raise AssertionError("Expected a workflow event mapping")
    return match.group(1)


def jobs(text):
    return dict(re.findall(r"(?m)^  ([a-z][a-z0-9-]*):\n((?:    .*\n|\n)+)", text.split("jobs:\n", 1)[1]))


class ManualCiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workflows = {path.name: path.read_text(encoding="utf-8") for path in WORKFLOWS.glob("*.yml")}
        cls.reusable = {name: text for name, text in cls.workflows.items()
                        if re.search(r"(?m)^  workflow_call:", events_block(text))}

    def test_automatic_events_run_only_lint_and_metadata_prompt(self):
        expected = {"pull_request": {"lint.yml"}, "push": {"lint.yml"},
                    "pull_request_target": {"full_ci_prompt.yml"}, "issue_comment": {"full_ci_request.yml"}}
        for event, expected_files in expected.items():
            with self.subTest(event=event):
                actual = {name for name, text in self.workflows.items()
                          if re.search(rf"(?m)^  {event}:", events_block(text))}
                self.assertEqual(actual, expected_files)
        self.assertIn("branches: [master]", events_block(self.workflows["lint.yml"]))
        self.assertNotRegex(events_block(self.workflows["ci.yml"]), r"(?m)^  (push|pull_request|schedule):")

    def test_pull_request_lint_is_unconditional_for_both_checks(self):
        text = self.workflows["lint.yml"]
        self.assertIn("types: [opened, synchronize, reopened, ready_for_review]", events_block(text))
        self.assertNotRegex(events_block(text), r"(?m)^    (?:paths|paths-ignore|branches-ignore):")
        self.assertEqual(set(jobs(text)), {"format", "tidy"})
        for body in jobs(text).values():
            self.assertNotRegex(body, r"(?m)^    (?:if|needs):")
        self.assertNotIn("run_lint", text)

    def test_full_bundle_covers_discovered_suites_without_profile_or_path_skips(self):
        calls = re.findall(r"(?m)^    uses: \./\.github/workflows/([^\s]+)$", self.workflows["ci.yml"])
        self.assertEqual(set(calls), set(self.reusable) - {"ci.yml"})
        self.assertEqual(len(calls), len(set(calls)))
        self.assertNotIn("release.yml", calls)
        for name, text in self.reusable.items():
            with self.subTest(workflow=name):
                self.assertRegex(events_block(text), r"(?m)^  workflow_dispatch:")
                self.assertNotIn("ci_plan", text)
                self.assertNotIn("ci-smoke", text)
                self.assertNotIn("inputs.profile", text)
                self.assertNotRegex(text, r"run_(?:cmake|coverage|cross_qemu|bazel|meson|xmake|measured):")
        self.assertFalse((ROOT / "scripts/ci_plan.py").exists())
        self.assertIn("run-name: CI (full) — ${{ github.ref_name }}", self.workflows["ci.yml"])

    def test_all_suites_have_distinct_pr_specific_concurrency(self):
        prefixes = []
        for name, text in self.reusable.items():
            with self.subTest(workflow=name):
                match = re.search(r"(?m)^  group: ([a-z-]+)-\$\{\{ inputs.pr_number && format\('pr-\{0\}', inputs.pr_number\) \|\| github.ref \}\}$", text)
                self.assertIsNotNone(match)
                prefixes.append(match[1])
                self.assertIn("cancel-in-progress: true", text)
        self.assertEqual(len(prefixes), len(set(prefixes)))
        for name in ("full_ci_prompt.yml", "full_ci_request.yml"):
            text = self.workflows[name]
            self.assertIn("group: full-ci-control-", text)
            self.assertNotIn("cancel-in-progress: true", text)

    def test_trusted_definitions_pass_pinned_commits_through_every_call(self):
        request = jobs(self.workflows["full_ci_request.yml"])
        self.assertIn("uses: ./.github/workflows/ci.yml", request["full"])
        for input_name in ("candidate_sha", "base_sha", "pr_number"):
            self.assertIn(input_name + ": ${{ needs.request.outputs." + input_name + " }}", request["full"])
        for name, body in jobs(self.workflows["ci.yml"]).items():
            if re.search(r"(?m)^    uses:", body):
                for input_name in ("candidate_sha", "base_sha", "pr_number"):
                    self.assertIn(input_name + ": ${{ needs.revision.outputs." + input_name + " }}", body)
        for name, text in self.workflows.items():
            self.assertNotIn("secrets: inherit", text)
            self.assertNotIn("actions: write", text)
        for name, text in self.reusable.items():
            self.assertIn("permissions:\n  contents: read", text)
            self.assertNotRegex(text, r"(?:pull-requests|issues|contents): write")

    def test_workers_checkout_candidate_after_trusted_provisioning(self):
        for name, text in self.reusable.items():
            for job, body in jobs(text).items():
                if "steps:" not in body or name == "ci.yml" or (name == "cmake.yml" and job == "plan") or job == "comparison-base":
                    continue
                with self.subTest(workflow=name, job=job):
                    candidate = "ref: ${{ inputs.candidate_sha || github.sha }}"
                    self.assertIn(candidate, body)
                    if "uses: ./.github/actions/setup-llvm" in body:
                        self.assertIn("ref: ${{ github.workflow_sha }}", body)
                        self.assertLess(body.index("ref: ${{ github.workflow_sha }}"), body.index("uses: ./.github/actions/setup-llvm"))
                        self.assertLess(body.index("uses: ./.github/actions/setup-llvm"), body.index(candidate))
        plan = jobs(self.workflows["cmake.yml"])["plan"]
        self.assertIn("ref: ${{ github.workflow_sha }}", plan)
        self.assertNotIn("inputs.candidate_sha", plan)
        for name, text in self.workflows.items():
            for block in re.findall(r"uses: actions/checkout@[^\n]+\n((?:        .*\n|\n)+)", text):
                # Source publication retains its separate signing/pushing permission boundary.
                if name != "release.yml":
                    self.assertIn("persist-credentials: false", block)

    def test_metadata_jobs_never_execute_candidate_code_or_poll(self):
        for name in ("full_ci_prompt.yml", "full_ci_request.yml"):
            text = self.workflows[name]
            self.assertNotIn("ref: ${{ inputs.candidate_sha", text)
            self.assertNotIn("ref: ${{ needs.request.outputs.candidate_sha", text)
            self.assertNotIn("github.event.comment.body", text)
            self.assertNotIn("workflow_dispatch", events_block(text))
        request = self.workflows["full_ci_request.yml"]
        self.assertIn("types: [created, edited]", events_block(request))
        self.assertIn("needs: [request, full]", request)
        self.assertIn("CI_RESULT: ${{ needs.full.result }}", request)
        prompt = self.workflows["full_ci_prompt.yml"]
        self.assertIn("types: [opened, synchronize, reopened, ready_for_review, edited]", events_block(prompt))
        controller = (ROOT / "scripts/full_ci.py").read_text()
        self.assertNotIn("author_association", controller)
        self.assertNotIn("sleep(", controller)
        self.assertNotIn("subprocess", controller)

    def test_candidate_caches_use_only_restore_actions(self):
        count = 0
        for name, text in self.reusable.items():
            for action in re.findall(r"uses: (actions/cache[^\s]+)", text):
                self.assertTrue(action.startswith("actions/cache/restore@"), (name, action))
                count += 1
            self.assertNotIn("cache-mode:", text)
        self.assertGreater(count, 0)

    def test_aggregate_rejects_skipped_cancelled_or_failed_suites(self):
        bundle = jobs(self.workflows["ci.yml"])
        aggregate = bundle["aggregate"]
        expected = set(bundle) - {"aggregate"}
        needs = set(re.search(r"needs: \[([^]]+)\]", aggregate)[1].split(", "))
        self.assertEqual(needs, expected)
        self.assertIn("if: ${{ always() }}", aggregate)
        script = textwrap.dedent(aggregate.split("python3 - <<'PYTHON'\n", 1)[1].split("          PYTHON", 1)[0])
        with tempfile.TemporaryDirectory() as tmp:
            results = {name: {"result": "success"} for name in expected}
            def run():
                env = {**os.environ, "CI_RESULTS": json.dumps(results), "GITHUB_STEP_SUMMARY": str(Path(tmp) / "summary")}
                return subprocess.run([sys.executable, "-c", script], env=env, capture_output=True, text=True)
            self.assertEqual(run().returncode, 0)
            for status in ("failure", "cancelled", "skipped"):
                with self.subTest(status=status):
                    results["cmake"]["result"] = status
                    result = run()
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("cmake", result.stderr)

    def test_measured_base_is_frozen_once_and_never_path_filtered(self):
        text = self.workflows["measured_reports.yml"]
        self.assertIn("CI_BASE_REF: ${{ inputs.base_sha || inputs.base_ref || 'master' }}", text)
        self.assertIn("CI_BASE_SHA: ${{ needs.comparison-base.outputs.base_sha }}", text)
        self.assertIn('git worktree add --detach "${GENTEST_MEASURED_BASE_SOURCE}" "${CI_BASE_SHA}"', text)
        self.assertNotIn("github.event.pull_request", text)

    def test_xmake_resource_locks_remain(self):
        text = (ROOT / "tests/CMakeLists.txt").read_text()
        self.assertEqual(text.count('PROPERTY RESOURCE_LOCK "xmake"'), 4)

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


    def test_package_pr_preset_is_focused_and_parallel(self) -> None:
        presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        test_presets = {preset["name"]: preset for preset in presets["testPresets"]}
        workflow_presets = {preset["name"]: preset for preset in presets["workflowPresets"]}

        parallel_base = test_presets["test-local-parallel-base"]
        self.assertEqual(parallel_base["execution"]["jobs"], 4)
        self.assertEqual(parallel_base["environment"]["GENTEST_HELPER_BUILD_PARALLEL_LEVEL"], "1")
        self.assertEqual(test_presets["release-package-pr"]["filter"]["include"]["label"], "^package$")

        package_pr_steps = workflow_presets["package-pr"]["steps"]
        self.assertEqual([step["type"] for step in package_pr_steps], ["configure", "build", "test", "package"])
        self.assertEqual(package_pr_steps[2]["name"], "release-package-pr")


if __name__ == "__main__":
    unittest.main()
