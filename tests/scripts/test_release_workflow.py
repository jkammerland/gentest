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

from test_setup_github_release_environment import find_bash

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "release.yml"
CMAKE_LISTS = ROOT / "CMakeLists.txt"
PRESETS = ROOT / "CMakePresets.json"
PACKAGE_SCRIPT = ROOT / "scripts" / "package_release.sh"


class ReleaseWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")
        cls.cmake_lists = CMAKE_LISTS.read_text(encoding="utf-8")
        cls.presets = PRESETS.read_text(encoding="utf-8")
        cls.package_script = PACKAGE_SCRIPT.read_text(encoding="utf-8")

    def test_signing_probe_runs_before_source_packaging(self) -> None:
        probe = self.workflow.index("- name: Verify release signing operation")
        package = self.workflow.index("- name: Package source archives and verify signatures")
        self.assertLess(probe, package)
        self.assertIn('--passphrase-file "${GPG_PASSPHRASE_FILE}"', self.workflow)

    def test_tag_is_signed_only_after_exact_successful_full_ci(self) -> None:
        ci_gate = self.workflow.index("- name: Verify successful full CI for exact commit")
        signing = self.workflow.index("- name: Sign and verify release tag")
        package = self.workflow.index("- name: Package source archives and verify signatures")
        self.assertLess(ci_gate, signing)
        self.assertLess(signing, package)
        self.assertIn('test "$(jq -r .conclusion <<< "${ci_run}")" = success', self.workflow)
        self.assertIn("= 'CI (full) — master'", self.workflow)
        self.assertIn(
            'test "$(jq -r .head_sha <<< "${ci_run}")" = "${release_commit}"',
            self.workflow,
        )
        self.assertIn('test "${release_commit}" = "$(git rev-parse origin/master)"', self.workflow)
        self.assertIn('tag -s "${RELEASE_TAG}"', self.workflow)
        self.assertIn('tag -s "${RELEASE_TAG}" -m "${RELEASE_TAG}"', self.workflow)
        self.assertIn('git verify-tag "${RELEASE_TAG}"', self.workflow)

    def test_release_title_matches_tag_for_new_and_resumed_drafts(self) -> None:
        self.assertEqual(self.workflow.count('-f name="${RELEASE_TAG}"'), 2)
        self.assertIn(
            'test "$(gh api "repos/${GITHUB_REPOSITORY}/releases/${release_id}" --jq .name)" = "${RELEASE_TAG}"',
            self.workflow,
        )
        self.assertNotIn("Gentest ${RELEASE_TAG}", self.workflow)

    def test_draft_updates_preserve_tag(self) -> None:
        patches = re.findall(r"gh api --method PATCH \\\n.*?--jq \.\w+", self.workflow, re.DOTALL)
        self.assertTrue(patches, "release draft updates must be exercised")
        bash = find_bash()
        self.assertIsNotNone(bash, "bash is required to exercise the release draft updates")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            state_file = root / "release.json"
            state_file.write_text(
                json.dumps(
                    {
                        "tag_name": "v9.8.7",
                        "name": "v9.8.7",
                        "target_commitish": "a" * 40,
                        "draft": True,
                    }
                ),
                encoding="utf-8",
            )
            mock = root / "github_api.py"
            mock.write_text(
                textwrap.dedent("""\
                import json
                import os
                import sys
                from pathlib import Path

                args = sys.argv[1:]
                assert args[0] == "api" and args[args.index("--method") + 1] == "PATCH"
                fields = {}
                for index, arg in enumerate(args):
                    if arg in {"-f", "-F"}:
                        key, value = args[index + 1].split("=", 1)
                        fields[key] = json.loads(value) if arg == "-F" else value
                path = Path(os.environ["MOCK_RELEASE_STATE"])
                state = json.loads(path.read_text(encoding="utf-8"))
                # Reproduce the observed GitHub draft behavior for a partial PATCH.
                state["tag_name"] = fields.get("tag_name", "untagged-placeholder")
                state.update(fields)
                path.write_text(json.dumps(state), encoding="utf-8")
                value = state[args[args.index("--jq") + 1].removeprefix(".")]
                print(json.dumps(value) if isinstance(value, bool) else value)
                """),
                encoding="utf-8",
            )
            env = os.environ.copy()
            env.pop("BASH_ENV", None)
            env.update(
                {
                    "MOCK_RELEASE_STATE": str(state_file),
                    "GITHUB_REPOSITORY": "example/gentest",
                    "RELEASE_TAG": "v9.8.7",
                    "RELEASE_COMMIT": "a" * 40,
                    "release_id": "123",
                }
            )
            launcher = """
            test_python="$1"
            mock_script="$2"
            if command -v cygpath >/dev/null 2>&1; then
              test_python="$(cygpath -u "$test_python")"
              mock_script="$(cygpath -u "$mock_script")"
            fi
            gh() { "$test_python" "$mock_script" "$@"; }
            """
            for patch in patches:
                result = subprocess.run(
                    [
                        bash,
                        "-c",
                        launcher + patch,
                        "gentest-release-update-test",
                        sys.executable,
                        str(mock),
                    ],
                    env=env,
                    check=False,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                state = json.loads(state_file.read_text(encoding="utf-8"))
                self.assertEqual(
                    state["tag_name"],
                    "v9.8.7",
                    f"draft update cleared its tag: {patch}",
                )
                self.assertEqual(state["target_commitish"], "a" * 40)
            self.assertFalse(state["draft"], "the publication update must be exercised")

    def test_only_regular_release_files_are_uploaded(self) -> None:
        self.assertIn("path: ${{ runner.temp }}/gentest-release/*.*", self.workflow)
        self.assertIn(
            'find "${artifact_dir}" -maxdepth 1 -type f -print0 | sort -z',
            self.workflow,
        )
        self.assertNotIn(
            'gh release create "${RELEASE_TAG}" "${RUNNER_TEMP}/gentest-release/"*',
            self.workflow,
        )

    def test_matching_draft_can_resume_but_published_release_cannot(self) -> None:
        self.assertIn("repos/${GITHUB_REPOSITORY}/releases?per_page=100", self.workflow)
        self.assertIn('release_rows="$(gh api --paginate', self.workflow)
        self.assertNotIn("mapfile -t matching_releases < <(\n            gh api", self.workflow)
        self.assertIn('test "${#matching_releases[@]}" -le 1', self.workflow)
        self.assertIn('test "${release_draft}" = true', self.workflow)
        self.assertIn('test "${release_tag}" = "${RELEASE_TAG}"', self.workflow)
        self.assertIn('test "${release_target}" = "${RELEASE_COMMIT}"', self.workflow)

    def test_draft_operations_use_numeric_release_id(self) -> None:
        self.assertNotIn("releases/tags/${RELEASE_TAG}", self.workflow)
        self.assertNotIn('gh release view "${RELEASE_TAG}"', self.workflow)
        self.assertNotIn('gh release upload "${RELEASE_TAG}"', self.workflow)
        self.assertNotIn('gh release edit "${RELEASE_TAG}"', self.workflow)
        self.assertIn(
            "https://uploads.github.com/repos/${GITHUB_REPOSITORY}/releases/${release_id}/assets",
            self.workflow,
        )
        self.assertIn("repos/${GITHUB_REPOSITORY}/releases/assets/${existing_id}", self.workflow)
        self.assertIn('release_id="$(gh api --method POST', self.workflow)

        verify_draft = self.workflow.index('--jq .draft)" = true')
        clear_draft = self.workflow.index("gh api --method DELETE", verify_draft)
        upload = self.workflow.index("curl --fail-with-body", clear_draft)
        self.assertLess(verify_draft, clear_draft)
        self.assertLess(clear_draft, upload)

    def test_publication_does_not_trace_the_actions_token(self) -> None:
        publish_step = self.workflow.index("- name: Stage and publish tag release")
        self.assertIn("set -euo pipefail", self.workflow[publish_step:])
        self.assertNotIn("set -euxo pipefail", self.workflow[publish_step:])

    def test_draft_is_published_only_after_exact_asset_verification(self) -> None:
        names = self.workflow.index('test "${local_names}" = "${remote_names}"')
        digests = self.workflow.index('test "${local_digest}" = "${remote_digest}"')
        publish = self.workflow.index("-F draft=false --jq .draft")
        self.assertLess(names, digests)
        self.assertLess(digests, publish)

    def test_public_release_uses_source_archives_for_all_platforms(self) -> None:
        self.assertIn("Signed source release", self.workflow)
        self.assertIn("scripts/package_source_release.py", self.workflow)
        self.assertIn('--ref "${RELEASE_COMMIT}"', self.workflow)
        self.assertIn("gentest-${{ inputs.tag }}-source", self.workflow)
        self.assertNotIn("scripts/package_release.sh", self.workflow)
        self.assertNotIn("host-developer-kit", self.workflow)
        self.assertNotIn("llvm-toolchain", self.workflow)
        self.assertNotIn("Setup vcpkg", self.workflow)

    def test_local_host_package_keeps_its_explicit_compatibility_label(self) -> None:
        self.assertIn("llvm${_gentest_release_llvm_major}-host-developer-kit", self.cmake_lists)
        self.assertIn('"GENTEST_RELEASE_HOST_DEVELOPER_KIT": "ON"', self.presets)
        self.assertIn("${artifact_dir}/${package_id}.manifest.json", self.package_script)
        self.assertIn("${artifact_dir}/${package_id}-${sbom_role}.spdx.json", self.package_script)


if __name__ == "__main__":
    unittest.main()
