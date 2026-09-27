#!/usr/bin/env python3
"""The diagnostic wrapper must not turn unrelated failures into XFAIL."""

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from probe_clang_leaks import LEAK_EXIT, classify, run_probe


def report(symbol="clang::DependentDiagnostic::Create(", size=31):
    return (
        "PROBE_COMPLETED dependent candidate\n"
        "==42==ERROR: LeakSanitizer: detected memory leaks\n\n"
        f"Direct leak of {size} byte(s) in 1 object(s) allocated from:\n"
        "    #0 0x123 in operator new(unsigned long)\n"
        f"    #1 0x456 in {symbol} example.cc:12\n\n"
        f"SUMMARY: AddressSanitizer: {size} byte(s) leaked in 1 allocation(s).\n"
    )


class LeakProbeTests(unittest.TestCase):
    def test_known_symbolized_leak(self):
        self.assertEqual(classify("dependent", LEAK_EXIT, report())[0], "XFAIL")
        # Allocation byte counts vary by library/ABI; they are not the identity.
        self.assertEqual(classify("dependent", LEAK_EXIT, report(size=1024))[0], "XFAIL")

    def test_control_and_unexpected_pass(self):
        self.assertEqual(classify("dependent", 0, "PROBE_COMPLETED dependent control\n", control=True)[0], "PASS")
        self.assertEqual(classify("dependent", 0, "PROBE_COMPLETED dependent candidate\n")[0], "XPASS")
        self.assertEqual(classify("dependent", LEAK_EXIT, report(), control=True)[0], "FAIL")

    def test_other_stack_or_missing_symbols(self):
        for symbol in ("gentest::someAllocation(", "(/lib/libclang-cpp.so+0x123)"):
            with self.subTest(symbol=symbol):
                self.assertEqual(classify("dependent", LEAK_EXIT, report(symbol))[0], "FAIL")

    def test_mixed_allocations_and_truncated_summary(self):
        unknown = "Direct leak of 8 byte(s) in 1 object(s) allocated from:\n    #0 0x123 in unrelated()\n\n"
        mixed = report().replace("SUMMARY:", unknown + "SUMMARY:").replace("31 byte(s) leaked in 1", "39 byte(s) leaked in 2")
        self.assertEqual(classify("dependent", LEAK_EXIT, mixed)[0], "FAIL")
        self.assertEqual(classify("dependent", LEAK_EXIT, report().split("SUMMARY:")[0])[0], "FAIL")
        self.assertEqual(classify("dependent", LEAK_EXIT, report().replace("31 byte(s) leaked", "99 byte(s) leaked"))[0], "FAIL")

    def test_process_failures_are_not_expected(self):
        for code in (None, -11, 1, 2, 124):
            with self.subTest(code=code):
                self.assertEqual(classify("dependent", code, report())[0], "FAIL")
        for extra in ("ERROR: AddressSanitizer: heap-use-after-free", "runtime error: overflow",
                      "WARNING: failed to symbolize", "AddressSanitizer:DEADLYSIGNAL"):
            with self.subTest(extra=extra):
                self.assertEqual(classify("dependent", LEAK_EXIT, report() + extra)[0], "FAIL")
        self.assertEqual(classify("dependent", LEAK_EXIT, report().replace("PROBE_COMPLETED", "NOT_COMPLETED"))[0], "FAIL")

    def test_missing_binary_is_a_setup_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            result = run_probe(Path(temp) / "missing", "dependent", Path(temp) / "result")
            self.assertEqual(result["status"], "FAIL")
            self.assertEqual(len(result["attempts"]), 1)
            self.assertIsNone(result["attempts"][0]["exit_code"])


if __name__ == "__main__":
    unittest.main()
