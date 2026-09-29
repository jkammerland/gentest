#!/usr/bin/env python3
"""Contract checks for the repository's manual CI entry points.

Actionlint validates YAML and workflow-call schemas; these checks enforce the
event and concurrency policy across the discovered workflow inventory.
"""

from pathlib import Path
import re
import unittest


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

    def test_pull_requests_do_not_start_workflows(self):
        self.assertTrue(self.workflows)
        for name, text in self.workflows.items():
            with self.subTest(workflow=name):
                events = re.findall(r"(?m)^  ([a-z_]+):", events_block(text))
                self.assertNotIn("pull_request", events)
                self.assertNotIn("pull_request_target", events)

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
