"""Check optimized helper probes, not a timing threshold or instruction count."""

import pathlib
import re
import sys


def check(path):
    assembly = pathlib.Path(path).read_text(encoding="utf-8")
    probes = (
        "scalar", "const_scalar", "rvalue_scalar", "const_rvalue", "pair",
        "const_pair", "rvalue_pair", "const_member", "array",
        "const_member_array", "noncopyable", "control_scalar", "control_pair",
    )

    # ELF/Mach-O/COFF compiler output: stop at the function end, not the next
    # unrelated function or a reference to the mix symbol in debug information.
    for name in probes:
        symbol = "gentest_probe_" + name
        start = re.search(r"^_?" + symbol + r"(?::|\s+PROC\b)[^\n]*\n", assembly, re.M)
        if start is None:
            raise AssertionError(f"missing probe definition: {symbol}")

        remainder = assembly[start.end():]
        end = re.search(
            r"^\s*(?:\.cfi_endproc|\.seh_endproc|\.size\s|Lfunc_end\d+:|[#;]\s*-- End function|"
            + symbol + r"\s+ENDP\b)", remainder, re.M,
        )
        if end is None:
            raise AssertionError(f"missing function boundary: {symbol}")

        body = remainder[:end.start()]
        calls_mix = re.search(
            r"\b(?:callq?|jmp|bl|b)\s+_?gentest_bench_util_mix\b", body,
        ) is not None
        expected = not name.startswith("control_")
        if calls_mix != expected:
            raise AssertionError(
                f"{symbol}: expected mix call={expected}, observed={calls_mix}\n{body}"
            )

    print(f"Whole-value optimization contract: {len(probes) - 2} probes, 2 elimination controls passed")


if __name__ == "__main__":
    check(sys.argv[1])
