#!/usr/bin/env python3

from __future__ import annotations

import unittest
from pathlib import Path


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

    def test_signing_probe_runs_before_expensive_package_build(self) -> None:
        probe = self.workflow.index("- name: Verify release signing operation")
        package = self.workflow.index("- name: Build, test, package, and verify signatures")
        self.assertLess(probe, package)
        self.assertIn('--passphrase-file "${GPG_PASSPHRASE_FILE}"', self.workflow)

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

    def test_transitional_artifact_is_not_named_as_a_portable_sdk(self) -> None:
        self.assertIn("Signed Linux/LLVM host developer kit", self.workflow)
        self.assertIn("linux-llvm-host-developer-kit", self.workflow)
        self.assertIn("llvm${_gentest_release_llvm_major}-host-developer-kit", self.cmake_lists)
        self.assertIn('"GENTEST_RELEASE_HOST_DEVELOPER_KIT": "ON"', self.presets)
        self.assertIn('${artifact_dir}/${package_id}.manifest.json', self.package_script)
        self.assertIn('${artifact_dir}/${package_id}-${sbom_role}.spdx.json', self.package_script)


if __name__ == "__main__":
    unittest.main()
