#!/usr/bin/env python3

from __future__ import annotations

import os
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "setup_github_release_environment.sh"
FINGERPRINT = "0123456789ABCDEF0123456789ABCDEF01234567"


class ReleaseEnvironmentSetupTests(unittest.TestCase):
    def make_environment(self, root: Path) -> dict[str, str]:
        bin_dir = root / "bin"
        bin_dir.mkdir()
        log = root / "calls.log"

        gh = bin_dir / "gh"
        gh.write_text(
            textwrap.dedent(
                """\
                #!/usr/bin/env bash
                set -euo pipefail
                case "$1:$2" in
                  auth:status) exit 0 ;;
                  repo:view)
                    repo="$3"
                    repo="${repo#https://github.com/}"
                    repo="${repo%.git}"
                    printf '%s\n' "$repo"
                    ;;
                  api:--method)
                    printf 'api %s\n' "$*" >> "$MOCK_LOG"
                    ;;
                  variable:set)
                    printf 'variable %s\n' "$*" >> "$MOCK_LOG"
                    ;;
                  variable:list)
                    printf 'GPG_FINGERPRINT\t%s\n' "$MOCK_FINGERPRINT"
                    ;;
                  secret:set)
                    payload="$(cat)"
                    printf 'secret %s bytes=%s\n' "$*" "${#payload}" >> "$MOCK_LOG"
                    ;;
                  secret:list)
                    printf 'GPG_PRIVATE_KEY\tupdated\nGPG_PASSPHRASE\tupdated\n'
                    ;;
                  *) printf 'unexpected gh invocation: %s\n' "$*" >&2; exit 90 ;;
                esac
                """
            ),
            encoding="utf-8",
        )
        gh.chmod(0o755)

        gpg = bin_dir / "gpg"
        gpg.write_text(
            textwrap.dedent(
                """\
                #!/usr/bin/env bash
                set -euo pipefail
                if [[ " $* " == *" --list-secret-keys "* ]]; then
                  printf 'sec:-:255:22:DEADBEEF:0:0:::::::\nfpr:::::::::%s:\n' "$MOCK_FINGERPRINT"
                elif [[ " $* " == *" --export-secret-keys "* ]]; then
                  printf '%s' 'PRIVATE-KEY-MATERIAL'
                else
                  printf 'unexpected gpg invocation: %s\n' "$*" >&2
                  exit 91
                fi
                """
            ),
            encoding="utf-8",
        )
        gpg.chmod(0o755)

        env = os.environ.copy()
        env.update(
            {
                "PATH": f"{bin_dir}:{env['PATH']}",
                "MOCK_LOG": str(log),
                "MOCK_FINGERPRINT": FINGERPRINT,
            }
        )
        return env

    def run_script(self, env: dict[str, str], *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [str(SCRIPT), *args],
            env=env,
            check=False,
            capture_output=True,
            text=True,
        )

    def test_dry_run_accepts_url_and_slug_without_writes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            env = self.make_environment(root)
            result = self.run_script(
                env,
                "--key",
                FINGERPRINT,
                "--no-passphrase",
                "--dry-run",
                "https://github.com/jkammerland/gentest.git",
                "jkammerland/cbor_tags",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("jkammerland/gentest", result.stdout)
            self.assertIn("jkammerland/cbor_tags", result.stdout)
            self.assertIn("Dry run", result.stdout)
            self.assertFalse((root / "calls.log").exists())

    def test_rejects_non_github_repository_url(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            env = self.make_environment(Path(tmp))
            result = self.run_script(
                env,
                "--key",
                FINGERPRINT,
                "--no-passphrase",
                "--dry-run",
                "https://gitlab.example.com/owner/repo",
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unsupported repository host", result.stderr)

    def test_configures_variable_private_key_and_passphrase(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            env = self.make_environment(root)
            passphrase_file = root / "passphrase"
            passphrase_file.write_text("correct horse battery staple\n", encoding="utf-8")
            passphrase_file.chmod(0o600)
            result = self.run_script(
                env,
                "--key",
                FINGERPRINT,
                "--passphrase-file",
                str(passphrase_file),
                "--yes",
                "git@github.com:jkammerland/gentest.git",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            calls = (root / "calls.log").read_text(encoding="utf-8")
            self.assertIn("environments/release", calls)
            self.assertIn("variable set GPG_FINGERPRINT", calls)
            self.assertIn("secret set GPG_PRIVATE_KEY", calls)
            self.assertIn("secret set GPG_PASSPHRASE", calls)
            self.assertNotIn("correct horse battery staple", calls)


if __name__ == "__main__":
    unittest.main()
