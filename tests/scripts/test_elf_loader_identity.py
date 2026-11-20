"""Check that the ABI assertion rejects a runnable consumer of a misnamed library."""

import subprocess
import sys
import tempfile
from pathlib import Path


def main(cxx, cmake, objdump, checker):
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        runtime = root / "libgentest_runtime.so.2"
        alternate = root / (runtime.name + ";alternate")
        consumer = root / "consumer"
        (root / "library.cpp").write_text("int value() { return 42; }\n")
        (root / "main.cpp").write_text("int value(); int main() { return value() != 42; }\n")

        # Keep the expected ABI available while the consumer imports only the alternate.
        for lib in (runtime, alternate):
            subprocess.run([cxx, "-shared", "-fPIC", f"-Wl,-soname,{lib.name}", "library.cpp", "-o", lib], cwd=root, check=True)
        subprocess.run([cxx, "main.cpp", alternate, f"-Wl,-rpath,{root}", "-o", consumer], cwd=root, check=True)
        subprocess.run([consumer], check=True)

        result = subprocess.run(
            [cmake, f"-DCONSUMER={consumer}", f"-DRUNTIME={runtime}", f"-DRUNTIME_DIR={root}", f"-DOBJDUMP={objdump}", "-P", checker],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        if result.returncode == 0 or f"Consumer imports '{alternate.name}'" not in result.stdout:
            raise AssertionError(f"Expected rejection of {alternate.name}, got {result.returncode}: {result.stdout}")


if __name__ == "__main__":
    main(*sys.argv[1:])
