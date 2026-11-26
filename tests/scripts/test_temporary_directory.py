"""Observe temporary directories across exits from generated cases."""
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from target_command import target_command


def run(root, case_name, expected_rc, message='', *args):
    env = {**os.environ, **dict.fromkeys(('TMPDIR', 'TEMP', 'TMP'), str(root))}
    case = f'temporary_directory/{case_name}'
    command = target_command(sys.argv[1], f'--run={case}', '--no-color', *args)
    result = subprocess.run(command, env=env, capture_output=True, text=True, encoding='utf-8', timeout=30)
    output = result.stdout + result.stderr
    assert result.returncode == expected_rc and case in output and message in output, (command, output)
    return result.stderr


def main(root):
    for name, code, message in [
        ('pass', 0, '[ PASS ]'),
        ('skip', 77, 'fixture skip'),
        ('cleanFailure', 1, 'fixture exception with cleanup'),
        ('cleanupAfterSetupFailure', 1, 'setup after directory creation failed'),
    ]:
        stderr = run(root, name, code, message, '--skip-return-code=77')
        assert 'case body should never run' not in stderr, stderr
        assert not list(root.iterdir()), list(root.iterdir())

    missing_root = root / 'missing'
    run(missing_root, 'pass', 1, '[ FAIL ]')
    assert not missing_root.exists()

    # Both processes keep their evidence under the same parent. Each gets a
    # different directory containing the file it wrote.
    with ThreadPoolExecutor(max_workers=2) as pool:
        exception = pool.submit(run, root, 'keepException', 1, 'fixture exception with retention')
        assertion = pool.submit(run, root, 'keepAssertion', 1, 'fatal fixture assertion')
        outputs = [exception.result(), assertion.result()]

    paths = set(root.iterdir())
    assert len(paths) == 2, paths
    for path in paths:
        assert (path / 'probe.txt').read_text() == 'fixture probe\n', path
    for output in outputs:
        assert sum(f' at {path}\n' in output for path in paths) == 1, output
    assert all(sum(f' at {path}\n' in output for output in outputs) == 1 for path in paths), outputs

    stderr = run(root, 'keepTeardown', 1, 'teardown after directory release failed')
    kept, = set(root.iterdir()) - paths
    assert f' at {kept}\n' in stderr and (kept / 'probe.txt').is_file(), stderr
    paths.add(kept)

    stderr = run(root, 'keepMeasurement', 1, 'measured call failed', '--kind=bench')
    kept, = set(root.iterdir()) - paths
    assert f' at {kept}\n' in stderr and kept.is_dir(), stderr


def check_noexceptions(root):
    env = {**os.environ, **dict.fromkeys(('TMPDIR', 'TEMP', 'TMP'), str(root))}
    case = 'regressions/local_fixture_teardown_noexceptions/fatal_assert'
    command = target_command(sys.argv[1], f'--run={case}', '--no-color')
    result = subprocess.run(command, env=env, capture_output=True, text=True, encoding='utf-8', timeout=30)
    assert result.returncode != 0 and 'terminating after fatal assertion' in result.stderr, result.stderr
    assert 'temporary directory ready' in result.stderr and 'local-fixture-teardown-noexc-marker' in result.stderr, result.stderr
    assert not list(root.iterdir()), list(root.iterdir())


if __name__ == '__main__':
    with tempfile.TemporaryDirectory() as directory:
        check = check_noexceptions if sys.argv[2:] == ['noexceptions'] else main
        check(Path(directory).resolve())
