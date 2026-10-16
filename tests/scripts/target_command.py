"""Build argv for a target executable launched by a native Python test driver."""

from __future__ import annotations

import json
import os


def target_command(binary: str | os.PathLike[str], *arguments: str) -> list[str]:
    raw = os.environ.get("GENTEST_TEST_EMULATOR_JSON", "[]")
    try:
        prefix = json.loads(raw)
    except json.JSONDecodeError as error:
        raise ValueError("GENTEST_TEST_EMULATOR_JSON must be a JSON argument array") from error
    if not isinstance(prefix, list) or any(not isinstance(item, str) or not item or "\x00" in item for item in prefix):
        raise ValueError("GENTEST_TEST_EMULATOR_JSON must contain nonempty string arguments")

    # Keep native invocation identical and preserve emulator argument boundaries.
    return [*prefix, os.fspath(binary), *arguments]
