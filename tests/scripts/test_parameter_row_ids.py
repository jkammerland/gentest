"""Check named rows through codegen, CLI selection, CTest, and JUnit."""
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from target_command import target_command
from test_runner_workflow import printed_command


def run(binary, *args, expected=0):
    command = target_command(binary, *args, '--no-color')
    result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', timeout=30)
    assert result.returncode == expected, (command, result.stdout, result.stderr)
    return result


def main(binary, ctest, build_dir, config):
    rows = {'empty': (0, 1), '\u03bb %5Ba%5D%3B%25%2F%5C': (2, 7)}
    payload_by_name = {
        f'row_ids/matrix<{kind}>[{name}][strict]({step})': f'row payload {numbers[0]} {numbers[1]} {step}'
        for kind in ('int', 'long') for name, numbers in rows.items() for step in (3, 4)
    }
    failed_name = 'row_ids/fails[bad input\'s "id"]'
    other_name = 'row_ids/fails[other]'
    inventory = json.loads(run(binary, '--list-json').stdout)
    assert sorted(case['name'] for case in inventory) == sorted([*payload_by_name, failed_name, other_name]), inventory

    # Exercise exact selection and confirm that each name invokes its own values.
    for name, payload in payload_by_name.items():
        result = run(binary, f'--run={name}')
        assert payload in result.stdout and 'Summary: passed 1/1;' in result.stdout, result.stdout

    command = [ctest, '--test-dir', build_dir, '--show-only=json-v1', '-R', '^row_ids/matrix']
    if config:
        command.extend(('-C', config))
    discovered = json.loads(subprocess.check_output(command, encoding='utf-8', timeout=30))['tests']
    assert sorted(case['name'] for case in discovered) == sorted(payload_by_name), discovered
    for case in discovered:
        assert case['command'][-2:] == [f'--gentest-run-exact={case["name"]}', '--skip-return-code=77'], case

    with tempfile.TemporaryDirectory() as directory:
        report = Path(directory) / 'rows.xml'
        run(binary, '--filter=row_ids/matrix*', f'--junit={report}')
        cases = ET.parse(report).findall('.//testcase')
        assert sorted(case.attrib['name'] for case in cases) == sorted(payload_by_name), cases

    failed = run(binary, f'--run={failed_name}', expected=1)
    commands = re.findall(r'^Rerun: (.+)$', failed.stderr, re.MULTILINE)
    assert len(commands) == 1, failed.stderr
    replay = printed_command(commands[0], expected=1)
    assert 'row value mismatch' in replay.stderr and failed_name in replay.stderr, replay.stderr


if __name__ == '__main__':
    main(*sys.argv[1:])
