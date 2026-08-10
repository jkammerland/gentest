#!/usr/bin/env python3
"""Shared, dependency-free helpers for compile benchmark scripts.

These helpers deliberately handle the shell command strings emitted by Ninja,
not arbitrary shell programs.  CMake's Ninja generator uses short ``cd ... &&
...`` chains for custom commands.  Keeping the parsing here means benchmark
scripts can change a codegen cap without accidentally changing a later tool in
the same chain.
"""
from __future__ import annotations

import math
import os
import re
import statistics
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


class CodegenCommandError(ValueError):
    """A requested codegen cap could not be proved effective."""


@dataclass(frozen=True)
class _ShellToken:
    value: str
    start: int
    end: int
    operator: bool = False


def parse_codegen_jobs(value: str | int) -> int:
    """Normalise a codegen worker cap; ``auto`` is the tool's ``0`` value."""
    raw = str(value).strip()
    if raw.lower() == "auto":
        return 0
    if not raw.isdecimal():
        raise CodegenCommandError(f"codegen jobs must be a non-negative integer or 'auto' (got {value!r})")
    return int(raw)


def _shell_tokens(command: str) -> list[_ShellToken]:
    """Tokenise the small POSIX/cmd command subset emitted by CMake/Ninja.

    The original spans are retained so rewriting one option never re-quotes or
    otherwise changes the caller's command.  In particular, Windows backslash
    paths and double-quoted paths with spaces remain byte-for-byte intact.
    """
    # CMake's Windows Ninja generator commonly stores the chain inside
    # ``cmd.exe /C "..."``.  Parse that payload recursively while retaining
    # offsets into the original command.
    wrapper = re.match(r'^\s*(?:"[^"]*cmd(?:\.exe)?"|\S*cmd(?:\.exe)?)\s+/[cC]\s+"(.*)"\s*$', command, re.IGNORECASE)
    if wrapper:
        offset = wrapper.start(1)
        return [
            _ShellToken(token.value, token.start + offset, token.end + offset, token.operator)
            for token in _shell_tokens(wrapper.group(1))
        ]

    tokens: list[_ShellToken] = []
    index = 0
    while index < len(command):
        if command[index].isspace():
            index += 1
            continue
        start = index
        for operator in ("&&", "||", ";", "|", "&"):
            if command.startswith(operator, index):
                tokens.append(_ShellToken(operator, index, index + len(operator), True))
                index += len(operator)
                break
        else:
            value: list[str] = []
            quote: str | None = None
            while index < len(command):
                character = command[index]
                if quote is None and (character.isspace() or character in ";|&"):
                    break
                if character in {"'", '"'}:
                    if quote is None:
                        quote = character
                        index += 1
                        continue
                    if quote == character:
                        quote = None
                        index += 1
                        continue
                # POSIX commands may escape spaces; Windows path separators
                # are kept unless the following character is shell syntax.
                if character == "\\" and quote != "'" and index + 1 < len(command):
                    following = command[index + 1]
                    if following.isspace() or following in {"'", '"', "\\", ";", "|", "&"}:
                        value.append(following)
                        index += 2
                        continue
                value.append(character)
                index += 1
            if quote is not None:
                raise CodegenCommandError("cannot parse Ninja command: unmatched quote")
            tokens.append(_ShellToken("".join(value), start, index))
            continue
        continue
    return tokens


def _segments(tokens: list[_ShellToken]) -> list[tuple[int, int]]:
    """Return [start, end) spans delimited by shell chain operators."""
    result: list[tuple[int, int]] = []
    start = 0
    for index, token in enumerate(tokens):
        if token.operator:
            if start != index:
                result.append((start, index))
            start = index + 1
    if start != len(tokens):
        result.append((start, len(tokens)))
    return result


def _is_codegen_executable(token: str) -> bool:
    # Pure ``Path`` on POSIX does not recognise a Windows backslash separator.
    name = token.replace("\\", "/").rsplit("/", 1)[-1].lower()
    return name in {"gentest_codegen", "gentest_codegen.exe"}


def codegen_job_values(command: str) -> list[int]:
    """Return the effective ``--jobs`` values in codegen command segments.

    A malformed jobs value is an error rather than an unverified benchmark
    setting.  Values belonging to other programs in a command chain are not
    considered.
    """
    tokens = _shell_tokens(command)
    values: list[int] = []
    for start, end in _segments(tokens):
        segment = tokens[start:end]
        if not any(_is_codegen_executable(token.value) for token in segment):
            continue
        for token in segment:
            if token.value.startswith("--jobs="):
                values.append(parse_codegen_jobs(token.value.split("=", 1)[1]))
    return values


def codegen_argument(command: str, name: str) -> str | None:
    """Return one argument value from a codegen segment, including spaced paths."""
    tokens = _shell_tokens(command)
    for start, end in _segments(tokens):
        segment = tokens[start:end]
        if not any(_is_codegen_executable(token.value) for token in segment):
            continue
        for index, token in enumerate(segment):
            if token.value == name:
                if index + 1 >= len(segment):
                    raise CodegenCommandError(f"{name} has no value in gentest_codegen command")
                return segment[index + 1].value
            prefix = f"{name}="
            if token.value.startswith(prefix):
                return token.value[len(prefix) :]
    return None


def rewrite_codegen_jobs(command: str, requested: str | int) -> tuple[str, dict[str, object]]:
    """Rewrite only ``gentest_codegen --jobs=…`` arguments in a Ninja command.

    The returned metadata is suitable for JSON provenance.  A requested cap is
    intentionally rejected when no codegen ``--jobs=`` token exists: otherwise
    callers could claim a cap that the process never received.
    """
    effective = parse_codegen_jobs(requested)
    tokens = _shell_tokens(command)
    replacements: list[tuple[int, int, str]] = []
    found_codegen = False
    for start, end in _segments(tokens):
        segment = tokens[start:end]
        if not any(_is_codegen_executable(token.value) for token in segment):
            continue
        found_codegen = True
        for index in range(start, end):
            token = tokens[index]
            if token.value.startswith("--jobs="):
                # Validate the original value too, so a malformed configured
                # command cannot hide behind a successful rewrite.
                parse_codegen_jobs(token.value.split("=", 1)[1])
                replacements.append((token.start, token.end, f"--jobs={effective}"))
    if not found_codegen:
        raise CodegenCommandError("command has no gentest_codegen executable segment")
    if not replacements:
        raise CodegenCommandError("cannot assert requested codegen cap: gentest_codegen command has no --jobs=<N> argument")
    rendered = command
    for start, end, replacement in reversed(replacements):
        rendered = f"{rendered[:start]}{replacement}{rendered[end:]}"
    asserted = codegen_job_values(rendered)
    if not asserted or any(value != effective for value in asserted):
        raise CodegenCommandError("cannot assert requested codegen cap after command rewrite")
    return rendered, {
        "requested": str(requested),
        "effective": effective,
        "rewritten_tokens": len(replacements),
        "effective_values": asserted,
    }


def rewrite_ninja_codegen_commands(build_ninja: Path, requested: str | int) -> list[dict[str, object]]:
    """Persistently rewrite every codegen ``COMMAND`` line in one Ninja file.

    This is intentionally narrow: only Ninja's ``COMMAND =`` assignments that
    contain a gentest_codegen command are changed.  It lets a subsequent
    ``cmake --build --clean-first`` execute the requested cap too, rather than
    measuring a manually invoked command at one cap and a CMake build at
    another.
    """
    if not build_ninja.exists():
        raise CodegenCommandError(f"build.ninja not found: {build_ninja}")
    lines = build_ninja.read_text(encoding="utf-8", errors="replace").splitlines(keepends=True)
    rewritten: list[dict[str, object]] = []
    changed = False
    for index, line in enumerate(lines):
        match = re.match(r"^(\s*COMMAND\s*=\s*)(.*?)(\r?\n?)$", line)
        if not match or "gentest_codegen" not in match.group(2):
            continue
        # The executable also provides validation subcommands, which have no
        # worker cap.  They are not gentest_codegen parse/emit invocations and
        # must not make a requested cap unverifiable.
        if not codegen_job_values(match.group(2)):
            continue
        command, metadata = rewrite_codegen_jobs(match.group(2), requested)
        lines[index] = f"{match.group(1)}{command}{match.group(3)}"
        rewritten.append(metadata)
        changed = True
    if not changed:
        raise CodegenCommandError(f"cannot assert requested codegen cap: no gentest_codegen COMMAND lines in {build_ninja}")
    build_ninja.write_text("".join(lines), encoding="utf-8")
    return rewritten


def ninja_codegen_command_file(build_ninja: Path, config: str | None = None) -> Path:
    """Resolve the Ninja file that owns custom commands for one configuration.

    CMake's Ninja Multi-Config generator keeps the actual ``COMMAND``
    assignments in ``CMakeFiles/impl-<Config>.ninja``.  The top-level
    ``build.ninja`` only dispatches to a default configuration, so rewriting it
    would silently leave ``cmake --build --config <Config>`` unchanged.
    """
    if config:
        implementation = build_ninja.parent / "CMakeFiles" / f"impl-{config}.ninja"
        if implementation.exists():
            return implementation
    return build_ninja


def ninja_codegen_job_values(build_ninja: Path) -> list[int]:
    """Collect configured parse/emit caps without changing the Ninja file."""
    values: list[int] = []
    for line in build_ninja.read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(r"^\s*COMMAND\s*=\s*(.*)$", line)
        if match and "gentest_codegen" in match.group(1):
            values.extend(codegen_job_values(match.group(1)))
    return values


@contextmanager
def temporary_ninja_codegen_commands(build_ninja: Path, requested: str | int):
    """Apply a verified cap for one build action, then restore the file.

    Restoring bytes, mode, and timestamps keeps the caller's configured build
    tree unchanged on success, benchmark failure, or interruption.
    """
    if not build_ninja.exists():
        raise CodegenCommandError(f"build.ninja not found: {build_ninja}")
    original = build_ninja.read_bytes()
    original_stat = build_ninja.stat()
    try:
        metadata = rewrite_ninja_codegen_commands(build_ninja, requested)
        yield metadata
        expected = parse_codegen_jobs(requested)
        values = ninja_codegen_job_values(build_ninja)
        if not values or any(value != expected for value in values):
            raise CodegenCommandError(
                f"cannot assert requested codegen cap after build: expected {expected}, found {values or 'none'}"
            )
    finally:
        build_ninja.write_bytes(original)
        os.chmod(build_ninja, original_stat.st_mode)
        os.utime(build_ninja, ns=(original_stat.st_atime_ns, original_stat.st_mtime_ns))


def median_mad(samples: list[float]) -> dict[str, object]:
    """Stable median/MAD summary while retaining raw samples for comparison."""
    if not samples:
        raise ValueError("at least one timed sample is required")
    finite_seconds(samples)
    center = statistics.median(samples)
    mad = statistics.median([abs(value - center) for value in samples])
    return {
        "samples_s": samples,
        "median_s": center,
        "mad_s": mad,
        "min_s": min(samples),
        "max_s": max(samples),
    }


def alternating_order(labels: list[str], rounds: int) -> list[list[str]]:
    """Alternate forward/reverse order to limit thermal and order bias."""
    if rounds < 0:
        raise ValueError("rounds must not be negative")
    if not labels:
        raise ValueError("at least one label is required")
    reverse = list(reversed(labels))
    return [list(labels if round_index % 2 == 0 else reverse) for round_index in range(rounds)]


def finite_seconds(values: Iterable[float]) -> None:
    if any(not math.isfinite(value) or value < 0 for value in values):
        raise ValueError("timing samples must be finite non-negative seconds")
