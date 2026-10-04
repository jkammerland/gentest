from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "ci_plan.py"
WORKFLOW_FILES = tuple(
    path for path in sorted((ROOT / ".github" / "workflows").glob("*.yml"))
    if "ci_plan_script=" in path.read_text(encoding="utf-8")
)
sys.path.insert(0, str(SCRIPT.parent))

import ci_plan  # noqa: E402
from test_setup_github_release_environment import find_bash  # noqa: E402


def expected(*enabled: str) -> dict[str, bool]:
    return {lane: lane in enabled for lane in ci_plan.LANES}


class PathClassificationTests(unittest.TestCase):
    def test_docs_only_skips_every_expensive_lane(self) -> None:
        self.assertEqual(
            ci_plan.classify_paths(["README.md", "docs/modules.md", "examples/README.md"]),
            expected(),
        )

    def test_measured_core_changes_enable_every_lane(self) -> None:
        paths = [
            "include/gentest/runner.h",
            "src/runner_impl.cpp",
            "tools/src/main.cpp",
            "cmake/GentestCodegen.cmake",
            "CMakeLists.txt",
        ]
        for path in paths:
            with self.subTest(path=path):
                self.assertEqual(ci_plan.classify_paths([path]), ci_plan.all_enabled())

    def test_unrelated_core_changes_skip_measured_reports(self) -> None:
        standard_lanes = tuple(lane for lane in ci_plan.LANES if lane != "measured")
        for path in ["tests/async/cases.cpp", "examples/runner_api.cpp"]:
            with self.subTest(path=path):
                self.assertEqual(ci_plan.classify_paths([path]), expected(*standard_lanes))

    def test_measured_report_inputs_run_measured_lane(self) -> None:
        cases = {
            "scripts/compare_measured_reports.py": expected("measured"),
            "scripts/ci_measured_report_compare.sh": expected("measured"),
            "tests/benchmarks/cases.cpp": ci_plan.all_enabled(),
            "tests/regressions/measured_report_coverage.cpp": ci_plan.all_enabled(),
            "tools/CMakeLists.txt": ci_plan.all_enabled(),
        }
        for path, expected_plan in cases.items():
            with self.subTest(path=path):
                self.assertEqual(ci_plan.classify_paths([path]), expected_plan)

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
        self.assertTrue(WORKFLOW_FILES, "path-selective workflows must be exercised")
        bash = find_bash()
        self.assertIsNotNone(bash, "bash is required to exercise the CI planning steps")
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            (repo / "scripts").mkdir()
            planner = repo / "scripts" / "ci_plan.py"
            fixture = """\
import sys
from pathlib import Path
output = Path(sys.argv[sys.argv.index('--github-output') + 1])
with output.open('a') as stream:
    stream.write('{marker}=true\\nrun_measured=true\\n')
"""
            planner.write_text(fixture.format(marker="base_planner_ran"), encoding="utf-8")
            subprocess.run(["git", "init", "--quiet", str(repo)], check=True)
            subprocess.run(["git", "-C", str(repo), "add", "scripts/ci_plan.py"], check=True)
            subprocess.run(
                [
                    "git", "-C", str(repo), "-c", "user.name=CI test", "-c", "user.email=ci@example.invalid",
                    "-c", "commit.gpgsign=false", "commit", "--quiet", "-m", "base planner",
                ],
                check=True,
            )
            base = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
            planner.write_text(fixture.format(marker="head_planner_ran"), encoding="utf-8")
            subprocess.run(["git", "-C", str(repo), "add", "scripts/ci_plan.py"], check=True)
            subprocess.run(
                [
                    "git", "-C", str(repo), "-c", "user.name=CI test", "-c", "user.email=ci@example.invalid",
                    "-c", "commit.gpgsign=false", "commit", "--quiet", "-m", "candidate planner",
                ],
                check=True,
            )
            head = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
            env = os.environ.copy()
            env.pop("BASH_ENV", None)
            env.update({
                "CI_EVENT_NAME": "pull_request",
                "CI_REF": "refs/pull/123/merge",
                "CI_BASE_SHA": base,
                "CI_HEAD_SHA": head,
                "RUNNER_TEMP": str(repo),
                "GITHUB_OUTPUT": str(repo / "outputs"),
                "CI_TEST_PYTHON": sys.executable,
            })
            launcher = """
            if command -v cygpath >/dev/null 2>&1; then
              RUNNER_TEMP="$(cygpath -u "$RUNNER_TEMP")"
              GITHUB_OUTPUT="$(cygpath -u "$GITHUB_OUTPUT")"
              CI_TEST_PYTHON="$(cygpath -u "$CI_TEST_PYTHON")"
              export RUNNER_TEMP GITHUB_OUTPUT
            fi
            python3() { "$CI_TEST_PYTHON" "$@"; }
            """
            for workflow in WORKFLOW_FILES:
                with self.subTest(workflow=workflow.name):
                    contents = workflow.read_text(encoding="utf-8").split("- name: Compute CI plan\n", 1)[1]
                    match = re.search(r"(?m)^        run: \|\n((?:          .*\n|\n)+)", contents)
                    self.assertIsNotNone(match, "the actual CI planning script must be exercised")
                    (repo / "outputs").unlink(missing_ok=True)
                    result = subprocess.run(
                        [bash, "-c", launcher + textwrap.dedent(match.group(1))],
                        cwd=repo, env=env, capture_output=True, text=True, check=False,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)
                    output = (repo / "outputs").read_text(encoding="utf-8")
                    self.assertIn("base_planner_ran=true", output)
                    self.assertNotIn("head_planner_ran=true", output)

    def test_cmake_selects_smoke_matrices_before_starting_workers(self) -> None:
        contents = (ROOT / ".github" / "workflows" / "cmake.yml").read_text(encoding="utf-8")
        self.assertIn("GENTEST_HELPER_BUILD_PARALLEL_LEVEL: 1", contents)
        bash_profiles = contents.count("--label-regex '^ci-smoke$'")
        powershell_profiles = contents.count('@("--label-regex", "^ci-smoke$")')
        self.assertGreaterEqual(bash_profiles + powershell_profiles, 3)
        self.assertNotIn('matrix.ci_exhaustive', contents)
        self.assertIn('scripts/ci_matrix.py --profile', contents)
        for host in ('linux', 'windows', 'macos'):
            self.assertIn('fromJSON(needs.plan.outputs.' + host + ')', contents)
        self.assertNotIn('CI planner failed; enabling all lanes', contents)
        self.assertIn("if: ${{ inputs.profile == 'full' }}", contents)

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

    def test_compatibility_labels_and_xmake_locks_are_declared(self) -> None:
        tests_cmake = (ROOT / "tests" / "CMakeLists.txt").read_text(encoding="utf-8")
        suite_helper = (ROOT / "cmake" / "GentestTests.cmake").read_text(encoding="utf-8")
        self.assertIn('default-suite;ci-compat', suite_helper)
        self.assertIn('set(_gentest_ci_compat_tests', tests_cmake)
        self.assertEqual(tests_cmake.count('PROPERTY RESOURCE_LOCK "xmake"'), 4)

    def test_measured_reports_are_path_scoped(self) -> None:
        contents = (ROOT / ".github" / "workflows" / "measured_reports.yml").read_text(encoding="utf-8")
        self.assertIn("name: Measured reports • plan", contents)
        self.assertIn("run_measured: ${{ steps.plan.outputs.run_measured }}", contents)
        self.assertIn('git show "${CI_BASE_SHA}:scripts/ci_plan.py"', contents)
        self.assertIn("Base-revision CI planner predates the measured lane", contents)
        self.assertIn("needs.plan.outputs.run_measured == 'true'", contents)


if __name__ == "__main__":
    unittest.main()
