from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "ci_plan.py"
WORKFLOW_FILES = (
    ROOT / ".github" / "workflows" / "cmake.yml",
    ROOT / ".github" / "workflows" / "lint.yml",
    ROOT / ".github" / "workflows" / "coverage.yml",
    ROOT / ".github" / "workflows" / "cross_qemu.yml",
    ROOT / ".github" / "workflows" / "buildsystems_linux.yml",
)
sys.path.insert(0, str(SCRIPT.parent))

import ci_plan  # noqa: E402


def expected(*enabled: str) -> dict[str, bool]:
    return {lane: lane in enabled for lane in ci_plan.LANES}


class PathClassificationTests(unittest.TestCase):
    def test_docs_only_skips_every_expensive_lane(self) -> None:
        self.assertEqual(
            ci_plan.classify_paths(["README.md", "docs/modules.md", "examples/README.md"]),
            expected(),
        )

    def test_core_changes_enable_every_lane(self) -> None:
        paths = [
            "include/gentest/runner.h",
            "src/runner_impl.cpp",
            "tools/src/main.cpp",
            "cmake/GentestCodegen.cmake",
            "CMakeLists.txt",
            "tests/async/cases.cpp",
        ]
        for path in paths:
            with self.subTest(path=path):
                self.assertEqual(ci_plan.classify_paths([path]), ci_plan.all_enabled())

    def test_lint_configuration_runs_lint(self) -> None:
        self.assertEqual(
            ci_plan.classify_paths([".clang-format", "scripts/check_clang_tidy.sh"]),
            expected("lint"),
        )

    def test_buildsystem_files_run_matching_lanes(self) -> None:
        cases = {
            "BUILD.bazel": expected("bazel"),
            "build_defs/gentest.bzl": expected("bazel"),
            "meson.build": expected("meson"),
            "meson/textual/meson.build": expected("meson"),
            "xmake.lua": expected("xmake"),
            "xmake/gentest.lua": expected("xmake"),
        }
        for path, plan in cases.items():
            with self.subTest(path=path):
                self.assertEqual(ci_plan.classify_paths([path]), plan)

    def test_common_buildsystem_helper_runs_all_buildsystem_lanes(self) -> None:
        self.assertEqual(
            ci_plan.classify_paths(["scripts/gentest_buildsystem_codegen.py"]),
            expected("bazel", "meson", "xmake"),
        )

    def test_dependency_changes_enable_every_lane(self) -> None:
        for path in ["vcpkg.json", "MODULE.bazel.lock", "requirements-dev.txt"]:
            with self.subTest(path=path):
                self.assertEqual(ci_plan.classify_paths([path]), ci_plan.all_enabled())

    def test_planner_and_workflow_changes_enable_every_lane(self) -> None:
        for path in ["scripts/ci_plan.py", ".github/workflows/new.yml"]:
            with self.subTest(path=path):
                self.assertEqual(ci_plan.classify_paths([path]), ci_plan.all_enabled())

    def test_unknown_empty_and_malformed_paths_enable_every_lane(self) -> None:
        for paths in [
            [],
            ["custom/config.yaml"],
            ["../outside"],
            [" docs/index.md"],
            [r"docs\index.md"],
            ["docs/unsafe\tname.md"],
        ]:
            with self.subTest(paths=paths):
                self.assertEqual(ci_plan.classify_paths(paths), ci_plan.all_enabled())

    def test_known_selective_paths_are_combined(self) -> None:
        self.assertEqual(
            ci_plan.classify_paths(["docs/index.md", ".clang-format", "BUILD.bazel", "xmake.lua"]),
            expected("lint", "bazel", "xmake"),
        )


class EventTests(unittest.TestCase):
    def test_required_workflows_run_for_stacked_pull_requests(self) -> None:
        stacked_pr_workflows = (
            ROOT / ".github" / "workflows" / "cmake.yml",
            ROOT / ".github" / "workflows" / "buildsystems_linux.yml",
            ROOT / ".github" / "workflows" / "lint.yml",
        )
        for workflow in stacked_pr_workflows:
            with self.subTest(workflow=workflow.name):
                contents = workflow.read_text(encoding="utf-8")
                self.assertIn("  pull_request:\n  workflow_dispatch:\n", contents)

        master_only_workflows = (
            ROOT / ".github" / "workflows" / "coverage.yml",
            ROOT / ".github" / "workflows" / "cross_qemu.yml",
            ROOT / ".github" / "workflows" / "measured_reports.yml",
        )
        for workflow in master_only_workflows:
            with self.subTest(workflow=workflow.name):
                contents = workflow.read_text(encoding="utf-8")
                self.assertIn("  pull_request:\n    branches: [ master ]\n", contents)

    def test_push_to_master_and_dispatch_enable_every_lane(self) -> None:
        docs = ["docs/index.md"]
        self.assertEqual(
            ci_plan.compute_plan(event_name="push", ref="refs/heads/master", paths=docs),
            ci_plan.all_enabled(),
        )
        self.assertEqual(
            ci_plan.compute_plan(event_name="workflow_dispatch", paths=docs),
            ci_plan.all_enabled(),
        )

    def test_unknown_event_and_missing_diff_enable_every_lane(self) -> None:
        self.assertEqual(
            ci_plan.compute_plan(event_name="schedule", paths=["docs/index.md"]),
            ci_plan.all_enabled(),
        )
        self.assertEqual(ci_plan.compute_plan(event_name="pull_request"), ci_plan.all_enabled())


class GitAndCliTests(unittest.TestCase):
    def git(self, repo: Path, *args: str) -> str:
        return subprocess.run(
            ["git", *args],
            cwd=repo,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        ).stdout.strip()

    def test_cli_uses_merge_base_and_writes_boolean_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo = Path(temporary_directory)
            self.git(repo, "init", "--initial-branch=master")
            self.git(repo, "config", "user.name", "CI Planner Test")
            self.git(repo, "config", "user.email", "ci-planner@example.invalid")
            self.git(repo, "config", "commit.gpgsign", "false")

            (repo / "README.md").write_text("base\n", encoding="utf-8")
            self.git(repo, "add", "README.md")
            self.git(repo, "commit", "-m", "base")

            self.git(repo, "checkout", "-b", "feature")
            (repo / "docs").mkdir()
            (repo / "docs" / "feature.md").write_text("feature docs\n", encoding="utf-8")
            self.git(repo, "add", "docs/feature.md")
            self.git(repo, "commit", "-m", "docs")
            head = self.git(repo, "rev-parse", "HEAD")

            self.git(repo, "checkout", "master")
            (repo / "src").mkdir()
            (repo / "src" / "base_only.cpp").write_text("// base advanced\n", encoding="utf-8")
            self.git(repo, "add", "src/base_only.cpp")
            self.git(repo, "commit", "-m", "advance base")
            base = self.git(repo, "rev-parse", "HEAD")

            output_file = repo / "github-output.txt"
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--event",
                    "pull_request",
                    "--repo",
                    str(repo),
                    "--base",
                    base,
                    "--head",
                    head,
                    "--github-output",
                    str(output_file),
                ],
                check=True,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

            expected_output = "\n".join(f"run_{lane}=false" for lane in ci_plan.LANES) + "\n"
            self.assertEqual(result.stdout, expected_output)
            self.assertEqual(output_file.read_text(encoding="utf-8"), expected_output)

    def test_unresolvable_diff_enables_every_lane(self) -> None:
        self.assertEqual(
            ci_plan.compute_plan(base="missing", head="also-missing", repo=ROOT),
            ci_plan.all_enabled(),
        )


class WorkflowContractTests(unittest.TestCase):
    def test_pull_requests_run_the_base_revision_planner(self) -> None:
        for workflow in WORKFLOW_FILES:
            with self.subTest(workflow=workflow.name):
                contents = workflow.read_text(encoding="utf-8")
                self.assertIn('if [ "${CI_EVENT_NAME}" = "pull_request" ]; then', contents)
                self.assertIn('git show "${CI_BASE_SHA}:scripts/ci_plan.py" > "${ci_plan_script}"', contents)
                self.assertIn('if [ -z "${ci_plan_script}" ] || ! python3 "${ci_plan_script}"', contents)


if __name__ == "__main__":
    unittest.main()
