#!/usr/bin/env python3

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "release.yml"


class ReleaseWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")

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
        self.assertIn("if gh release view", self.workflow)
        self.assertIn("--jq .draft)\" = true", self.workflow)
        self.assertIn("--jq .tag_name)\" = \"${RELEASE_TAG}\"", self.workflow)
        self.assertIn("--jq .target_commitish)\" = \"${RELEASE_COMMIT}\"", self.workflow)
        self.assertIn('gh release upload "${RELEASE_TAG}" "${release_assets[@]}" --clobber', self.workflow)

    def test_draft_is_published_only_after_exact_asset_verification(self) -> None:
        names = self.workflow.index('test "${local_names}" = "${remote_names}"')
        digests = self.workflow.index('test "${local_digest}" = "${remote_digest}"')
        publish = self.workflow.index('gh release edit "${RELEASE_TAG}" --draft=false')
        self.assertLess(names, digests)
        self.assertLess(digests, publish)


if __name__ == "__main__":
    unittest.main()
