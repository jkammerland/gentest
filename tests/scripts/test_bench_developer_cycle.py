#!/usr/bin/env python3
"""Protect benchmark inventory, Ninja accounting, and failure integrity."""

import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import bench_developer_cycle as benchmark


class DeveloperCycleBenchmarkTests(unittest.TestCase):
    def test_ninja_multi_output_edges_are_counted_once(self):
        actions = benchmark.ninja_actions([
            "# ninja log v5",
            "0\t200\t0\tgenerated_mock_registry.hpp\taaaa",
            "0\t200\t0\tgenerated_mock_impl.hpp\taaaa",
            "0\t200\t0\tgentest/src/runner.cpp.o\tbbbb",
        ])
        self.assertEqual(len(actions), 2)
        summary = benchmark.action_summary(actions)
        self.assertEqual(summary["mock_generation"], {"actions": 1, "action_seconds": 0.2})
        self.assertEqual(summary["runtime_construction"], {"actions": 1, "action_seconds": 0.2})

    def test_inventory_rejects_vacuous_missing_extra_and_duplicate_cases(self):
        expected = {"suite/a", "suite/b"}
        benchmark.validate_inventory(["suite/b", "suite/a"], ["suite/a", "suite/b"], expected)
        for runner, ctest in (
            ([], []),
            (["suite/a"], ["suite/a", "suite/b"]),
            (["suite/a", "suite/b"], ["suite/a"]),
            (["suite/a", "suite/b", "suite/c"], ["suite/a", "suite/b"]),
            (["suite/a", "suite/a"], ["suite/a", "suite/b"]),
            (["suite/a", "suite/b"], ["suite/a", "suite/b", "suite/b"]),
        ):
            with self.subTest(runner=runner, ctest=ctest), self.assertRaises(ValueError):
                benchmark.validate_inventory(runner, ctest, expected)
        with self.assertRaises(ValueError):
            benchmark.validate_inventory([], [], set())

    def test_registration_outputs_are_not_mistaken_for_mock_generation(self):
        self.assertEqual(benchmark.action_category([
            "gentest/cycle_tests/tu_0000_cases.header_registration.gentest.cpp",
            "gentest/cycle_tests/cycle_tests_mock_registry.hpp",
            "gentest/cycle_tests/cycle_tests_mock_impl.hpp",
        ]), "registration_generation")
        self.assertEqual(benchmark.action_category([
            "generated/mocks/tu_0000_defs.header_registration.gentest.cpp",
            "generated/mocks/cycle_mocks_mock_registry.hpp",
        ]), "mock_generation")

    def test_cmake_regeneration_is_not_runtime_construction(self):
        actions = benchmark.ninja_actions([
            "1\t241\t0\tbuild.ninja\taaaa",
            "1\t241\t0\t/build/gentest/src/cmake_install.cmake\taaaa",
            "1\t241\t0\t/build/gentest/src/CTestTestfile.cmake\taaaa",
        ])
        self.assertEqual(benchmark.action_summary(actions), {
            "cmake_regeneration": {"actions": 1, "action_seconds": 0.24},
        })

    def test_compacted_log_does_not_recount_old_actions(self):
        old = "0\t200\t1\tcases.cpp.o\taaaa"
        retained = "0\t300\t2\tfixtures.cpp.o\tbbbb"
        rebuilt = "10\t400\t3\tcases.cpp.o\tcccc"
        linked = "400\t450\t4\tcycle_tests\tdddd"
        before = ["# ninja log v5", old, retained, old]
        after = ["# ninja log v5", retained, rebuilt, linked]
        self.assertEqual(benchmark.new_ninja_lines(before, after), [rebuilt, linked])
        self.assertEqual(len(benchmark.ninja_actions(benchmark.new_ninja_lines(before, after))), 2)

    def test_edit_anchor_failures_preserve_the_workload(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "cases.cpp"
            for contents in ("unrelated source", "anchor anchor"):
                path.write_text(contents)
                with self.assertRaises(ValueError):
                    benchmark.replace_once(path, "anchor", "changed")
                self.assertEqual(path.read_text(), contents)

    def test_failed_commands_keep_exit_status_and_diagnostics(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            record = {"sample": 1, "scenario": "cold", "phases": []}
            log = root / "failure.log"
            with self.assertRaisesRegex(RuntimeError, "exited 7"):
                benchmark.timed(
                    [sys.executable, "-c", "print('deliberate build failure'); raise SystemExit(7)"],
                    root, dict(os.environ), log, record, "consumer_build",
                )
            self.assertEqual(record["phases"][0]["returncode"], 7)
            self.assertIn("deliberate build failure", log.read_text())
            self.assertGreaterEqual(record["phases"][0]["seconds"], 0)


if __name__ == "__main__":
    unittest.main()
