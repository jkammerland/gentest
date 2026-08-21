#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  setup_github_release_environment.sh [options] <github-repo> [<github-repo> ...]

Configure a GitHub Actions release environment from the current Unix user's
GnuPG keyring. Repository arguments may be owner/name, HTTPS URLs, or GitHub
SSH URLs.

Options:
  --key <selector>          GPG fingerprint, key ID, or UID. When omitted,
                            RELEASE_GPG_FINGERPRINT is used, or the only secret
                            key in the keyring is selected.
  --environment <name>     GitHub environment name (default: release).
  --prompt-passphrase      Prompt once and store GPG_PASSPHRASE when nonempty.
  --passphrase-file <path> Read the passphrase from a local file.
  --no-passphrase          Do not create or update GPG_PASSPHRASE.
  --yes                    Skip the final confirmation prompt.
  --dry-run                Validate and print planned changes without writing.
  -h, --help               Show this help.

The script creates/updates:
  Environment variable: GPG_FINGERPRINT
  Environment secret:   GPG_PRIVATE_KEY
  Environment secret:   GPG_PASSPHRASE (only when supplied)
EOF
}

die() {
  echo "error: $*" >&2
  exit 1
}

normalize_repo() {
  local value="$1"
  value="${value%/}"
  value="${value%.git}"

  case "${value}" in
    https://github.com/* | http://github.com/*)
      value="${value#*://github.com/}"
      ;;
    git@github.com:*)
      value="${value#git@github.com:}"
      ;;
    ssh://git@github.com/*)
      value="${value#ssh://git@github.com/}"
      ;;
    github.com/*)
      value="${value#github.com/}"
      ;;
    *://* | *@*:*)
      die "unsupported repository host or URL: $1"
      ;;
  esac

  if [[ ! "${value}" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]]; then
    die "expected a GitHub repository as owner/name or a github.com URL: $1"
  fi
  printf '%s\n' "${value}"
}

resolve_fingerprint() {
  local selector="$1"
  local -a fingerprints=()

  if [[ -n "${selector}" ]]; then
    mapfile -t fingerprints < <(
      gpg --batch --with-colons --list-secret-keys "${selector}" 2>/dev/null |
        awk -F: '$1 == "sec" { want_fpr = 1; next } want_fpr && $1 == "fpr" { print toupper($10); exit }'
    )
  else
    mapfile -t fingerprints < <(
      gpg --batch --with-colons --list-secret-keys 2>/dev/null |
        awk -F: '$1 == "sec" { want_fpr = 1; next } want_fpr && $1 == "fpr" { print toupper($10); want_fpr = 0 }'
    )
  fi

  if [[ "${#fingerprints[@]}" -eq 0 ]]; then
    die "no matching GPG secret key found for Unix user $(id -un)"
  fi
  if [[ -z "${selector}" && "${#fingerprints[@]}" -ne 1 ]]; then
    die "found ${#fingerprints[@]} GPG secret keys; select one with --key or RELEASE_GPG_FINGERPRINT"
  fi
  if [[ ! "${fingerprints[0]}" =~ ^[0-9A-F]{40,64}$ ]]; then
    die "GPG returned an invalid primary-key fingerprint: ${fingerprints[0]}"
  fi
  printf '%s\n' "${fingerprints[0]}"
}

environment_name="release"
key_selector="${RELEASE_GPG_FINGERPRINT:-}"
passphrase_mode="auto"
passphrase_file=""
assume_yes=false
dry_run=false
repo_inputs=()

while (($#)); do
  case "$1" in
    --key)
      (($# >= 2)) || die "--key requires a value"
      key_selector="$2"
      shift 2
      ;;
    --environment)
      (($# >= 2)) || die "--environment requires a value"
      environment_name="$2"
      shift 2
      ;;
    --prompt-passphrase)
      passphrase_mode="prompt"
      shift
      ;;
    --passphrase-file)
      (($# >= 2)) || die "--passphrase-file requires a path"
      passphrase_mode="file"
      passphrase_file="$2"
      shift 2
      ;;
    --no-passphrase)
      passphrase_mode="none"
      shift
      ;;
    --yes)
      assume_yes=true
      shift
      ;;
    --dry-run)
      dry_run=true
      shift
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    --*)
      die "unknown option: $1"
      ;;
    *)
      repo_inputs+=("$1")
      shift
      ;;
  esac
done

((${#repo_inputs[@]} > 0)) || {
  usage >&2
  exit 2
}
[[ "${environment_name}" =~ ^[A-Za-z0-9_.-]+$ ]] || die "invalid environment name: ${environment_name}"

command -v gh >/dev/null || die "GitHub CLI (gh) is not installed"
command -v gpg >/dev/null || die "GnuPG (gpg) is not installed"
gh auth status >/dev/null 2>&1 || die "GitHub CLI is not authenticated; run 'gh auth login' as $(id -un)"

fingerprint="$(resolve_fingerprint "${key_selector}")"
repos=()
for input in "${repo_inputs[@]}"; do
  repo="$(normalize_repo "${input}")"
  canonical_repo="$(gh repo view "${repo}" --json nameWithOwner --jq .nameWithOwner 2>/dev/null)" ||
    die "cannot access GitHub repository ${repo} as $(id -un)"
  if [[ ! "${canonical_repo}" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]]; then
    die "GitHub returned an invalid repository name for ${repo}: ${canonical_repo}"
  fi
  if [[ ! " ${repos[*]} " =~ " ${canonical_repo} " ]]; then
    repos+=("${canonical_repo}")
  fi
done

passphrase=""
if [[ "${passphrase_mode}" == "auto" ]]; then
  if [[ -t 0 ]]; then
    passphrase_mode="prompt"
  else
    passphrase_mode="none"
  fi
fi
if [[ "${passphrase_mode}" == "prompt" ]]; then
  read -rsp "GPG passphrase (leave empty for an unprotected key): " passphrase
  echo
elif [[ "${passphrase_mode}" == "file" ]]; then
  [[ -f "${passphrase_file}" && -r "${passphrase_file}" ]] || die "passphrase file is not readable: ${passphrase_file}"
  IFS= read -r passphrase < "${passphrase_file}" || true
fi

echo "Unix user:  $(id -un)"
echo "GPG key:    ${fingerprint}"
echo "Environment: ${environment_name}"
printf 'Repositories:\n'
printf '  - %s\n' "${repos[@]}"
if [[ "${passphrase_mode}" == "none" || -z "${passphrase}" ]]; then
  echo "Passphrase: not updated"
else
  echo "Passphrase: will be stored as an environment secret"
fi

if "${dry_run}"; then
  echo "Dry run: no GitHub settings changed."
  exit 0
fi

if ! "${assume_yes}"; then
  read -rp "Configure these GitHub release environments? [y/N] " answer
  [[ "${answer}" =~ ^[Yy]$ ]] || die "cancelled"
fi

if [[ -n "${passphrase}" ]]; then
  private_key="$(gpg --batch --yes --pinentry-mode loopback --passphrase-fd 3 \
    --armor --export-secret-keys "${fingerprint}" 3<<<"${passphrase}")"
else
  private_key="$(gpg --batch --armor --export-secret-keys "${fingerprint}")"
fi
if [[ "${private_key}" != *"-----BEGIN PGP PRIVATE KEY BLOCK-----"* ||
      "${private_key}" != *"-----END PGP PRIVATE KEY BLOCK-----"* ]]; then
  unset private_key passphrase
  die "GPG did not export an armored private key; check the selected key and passphrase (no GitHub settings were changed)"
fi

for repo in "${repos[@]}"; do
  echo "Configuring ${repo}..."
  gh api --method PUT "repos/${repo}/environments/${environment_name}" >/dev/null
  gh variable set GPG_FINGERPRINT \
    --repo "${repo}" \
    --env "${environment_name}" \
    --body "${fingerprint}"
  printf '%s\n' "${private_key}" |
    gh secret set GPG_PRIVATE_KEY \
      --repo "${repo}" \
      --env "${environment_name}"
  if [[ -n "${passphrase}" ]]; then
    printf '%s' "${passphrase}" |
      gh secret set GPG_PASSPHRASE \
        --repo "${repo}" \
        --env "${environment_name}"
  fi

  echo "Configured ${repo}/${environment_name}:"
  gh variable list --repo "${repo}" --env "${environment_name}" | awk '$1 == "GPG_FINGERPRINT" { print "  variable: " $1 "=" $2 }'
  gh secret list --repo "${repo}" --env "${environment_name}" | awk '$1 ~ /^GPG_/ { print "  secret:   " $1 }'
done

unset private_key passphrase
echo "Release environment setup complete."
