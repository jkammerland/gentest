"""Controlled LLVM installations for tool-selection regressions."""
from pathlib import Path


def fake_toolchain(prefix: Path, major: int) -> Path:
    directory = prefix / "bin"
    directory.mkdir(parents=True)
    for name in ("clang", "clang++", "clang-tidy", "clang-format", "clang-scan-deps", "llvm-cov", "llvm-config"):
        tool = directory / name
        cmakedir = str(prefix / "lib/cmake/llvm").replace("'", "'\"'\"'")
        tool.write_text("#!/bin/sh\n" +
                        (f"if [ \"$1\" = --cmakedir ]; then printf '%s\\n' '{cmakedir}'; exit; fi\n" if name == "llvm-config" else "") +
                        f"printf '%s\\n' 'LLVM version {major}.1.0'\n")
        tool.chmod(0o755)
    for package, name in (("llvm", "LLVM"), ("clang", "Clang")):
        directory = prefix / "lib/cmake" / package
        directory.mkdir(parents=True)
        (directory / f"{name}Config.cmake").write_text("# test package\n")
        (directory / f"{name}ConfigVersion.cmake").write_text(f'set(PACKAGE_VERSION "{major}.1.0")\n')
    return prefix
