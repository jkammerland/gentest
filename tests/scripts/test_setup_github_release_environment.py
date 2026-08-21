#!/usr/bin/env python3

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "setup_github_release_environment.sh"
FINGERPRINT = "0123456789ABCDEF0123456789ABCDEF01234567"


def find_bash() -> str | None:
    resolved = shutil.which("bash")
    if resolved is not None:
        return resolved
    if os.name != "nt":
        return None

    candidates: list[Path] = []
    git = shutil.which("git")
    if git is not None:
        candidates.append(Path(git).resolve().parent.parent / "bin" / "bash.exe")
    for variable in ("ProgramW6432", "ProgramFiles", "ProgramFiles(x86)"):
        prefix = os.environ.get(variable)
        if prefix:
            candidates.append(Path(prefix) / "Git" / "bin" / "bash.exe")
    return next((str(candidate) for candidate in candidates if candidate.is_file()), None)


BASH = find_bash()


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
                  if [[ -n "${MOCK_SECOND_FINGERPRINT:-}" ]]; then
                    printf 'sec:-:255:22:DEADBEEF:0:0:::::::\nfpr:::::::::%s:\n' "$MOCK_SECOND_FINGERPRINT"
                  fi
                elif [[ " $* " == *" --export-secret-keys "* ]]; then
                  if [[ " $* " == *" --passphrase-fd 3 "* ]]; then
                    IFS= read -r supplied_passphrase <&3
                    [[ "$supplied_passphrase" == "$MOCK_PASSPHRASE" ]] || exit 92
                  fi
                  if [[ "${MOCK_EMPTY_EXPORT:-false}" != "true" ]]; then
                    printf '%s\n' \
                      '-----BEGIN PGP PRIVATE KEY BLOCK-----' \
                      'PRIVATE-KEY-MATERIAL' \
                      '-----END PGP PRIVATE KEY BLOCK-----'
                  fi
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
        env.pop("BASH_ENV", None)
        env.update(
            {
                "HOME": str(root),
                "PATH": f"{bin_dir}{os.pathsep}{env['PATH']}",
                "MOCK_BIN": str(bin_dir),
                "MOCK_LOG": str(log),
                "MOCK_FINGERPRINT": FINGERPRINT,
                "MOCK_PASSPHRASE": "correct horse battery staple",
            }
        )
        return env

    def run_script(self, env: dict[str, str], *args: str) -> subprocess.CompletedProcess[str]:
        if BASH is None:
            self.fail("bash is required to exercise the release environment setup script")
        launcher = """
        mock_bin="$1"
        shift
        if command -v cygpath >/dev/null 2>&1; then
          mock_bin="$(cygpath -u "$mock_bin")"
        fi
        PATH="$mock_bin:$PATH"
        export PATH
        GENTEST_GH_COMMAND="$mock_bin/gh"
        GENTEST_GPG_COMMAND="$mock_bin/gpg"
        export GENTEST_GH_COMMAND GENTEST_GPG_COMMAND
        script="$1"
        shift
        source "$script" "$@"
        """
        return subprocess.run(
            [BASH, "-c", launcher, "gentest-release-setup-test", env["MOCK_BIN"], str(SCRIPT), *args],
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
                "git@github.com:jkammerland/gentest.git",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("jkammerland/gentest", result.stdout)
            self.assertIn("jkammerland/cbor_tags", result.stdout)
            self.assertEqual(result.stdout.count("  - jkammerland/gentest\n"), 1)
            self.assertIn("Dry run", result.stdout)
            self.assertFalse((root / "calls.log").exists())

    def test_requires_key_selector_when_multiple_secret_keys_exist(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            env = self.make_environment(root)
            env["MOCK_SECOND_FINGERPRINT"] = "89ABCDEF0123456789ABCDEF0123456789ABCDEF"
            result = self.run_script(
                env,
                "--no-passphrase",
                "--dry-run",
                "jkammerland/gentest",
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("found 2 GPG secret keys", result.stderr)
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
            passphrase_file.write_bytes(b"correct horse battery staple\r\n")
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

    def test_empty_gpg_export_fails_before_github_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            env = self.make_environment(root)
            env["MOCK_EMPTY_EXPORT"] = "true"
            result = self.run_script(
                env,
                "--key",
                FINGERPRINT,
                "--no-passphrase",
                "--yes",
                "jkammerland/gentest",
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("did not export an armored private key", result.stderr)
            self.assertFalse((root / "calls.log").exists())


if __name__ == "__main__":
    unittest.main()
