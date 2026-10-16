"""Runtime ownership regressions against configured native target metadata."""
from __future__ import annotations

import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import runtime_sources as checker


class OwnershipTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name).resolve()
        (self.source / "src").mkdir()
        for name in ("benchmark.cpp", "bench_util.cpp", "runner_impl.cpp", "gentest_main.cpp", "gentest_anchor.cpp"):
            (self.source / "src" / name).touch()
        self.runtime = ["src/benchmark.cpp", "src/runner_impl.cpp"]
        self.escape = ["src/bench_util.cpp"]

    def cmake(self, runtime, escape, test=()):
        reply = self.source / "build/.cmake/api/v1/reply"
        reply.mkdir(parents=True, exist_ok=True)
        targets = []
        for name, sources in (("gentest_runtime", runtime), ("gentest_bench_escape", escape), ("test_only", test)):
            targets.append({"name": name, "id": name, "jsonFile": name + ".json"})
            target = {"name": name, "sources": [{"path": path} for path in sources]}
            if name == "gentest_runtime":
                target["dependencies"] = [{"id": "gentest_bench_escape"}]
            (reply / (name + ".json")).write_text(json.dumps(target))
        (reply / "model.json").write_text(json.dumps({"configurations": [{"name": "", "targets": targets}]}))
        (reply / "index-1.json").write_text(json.dumps({"reply": {"codemodel-v2": {"jsonFile": "model.json"}}}))
        return checker.cmake_sources(self.source / "build")

    def meson(self, runtime, escape, test=()):
        return checker.meson_sources([{"name": name, "target_sources": [{"language": "cpp", "sources": list(sources)}]}
            for name, sources in (("gentest_runtime", runtime), ("gentest_bench_escape", escape), ("test_only", test))])

    def bazel(self, runtime, escape, test=()):
        metadata = {"targets": [{"id": "1", "label": "//:gentest_runtime"}, {"id": "2", "label": "//:gentest_bench_escape"}],
                    "actions": []}
        # deps(runtime) excludes unrelated test actions at the query boundary.
        for target, sources in (("1", runtime), ("2", escape)):
            metadata["actions"] += [{"targetId": target, "mnemonic": "CppCompile", "arguments": ["clang++", "-c", path]}
                                    for path in sources]
        return checker.bazel_sources(metadata)

    def xmake(self, runtime, escape, test=()):
        return checker.xmake_sources({"name": "gentest_runtime", "sourcebatches": [
            {"rulename": "c++.build", "sourcekind": "cxx", "sourcefiles": list(runtime) + list(escape)}]})

    def adapters(self):
        return (self.cmake, self.meson, self.bazel, self.xmake)

    def test_each_backend_owns_runtime_and_escape_once(self):
        for adapter in self.adapters():
            with self.subTest(backend=adapter.__name__):
                self.assertEqual(checker.check_ownership(self.source, adapter(self.runtime, self.escape)), 3)

    def test_removed_benchmark_and_test_only_ownership_fail(self):
        for adapter in self.adapters():
            for test in ((), ("src/benchmark.cpp",)):
                with self.subTest(backend=adapter.__name__, test=test):
                    with self.assertRaisesRegex(ValueError, "Unowned runtime sources: src/benchmark.cpp"):
                        checker.check_ownership(self.source, adapter(["src/runner_impl.cpp"], self.escape, test))

    def test_new_unowned_runtime_source_fails_without_a_maintained_inventory(self):
        (self.source / "src/new_runtime.cpp").touch()
        for adapter in self.adapters():
            with self.subTest(backend=adapter.__name__), self.assertRaisesRegex(ValueError, "src/new_runtime.cpp"):
                checker.check_ownership(self.source, adapter(self.runtime, self.escape))

    def test_duplicate_ownership_and_entry_point_sources_fail(self):
        for adapter in self.adapters():
            with self.subTest(backend=adapter.__name__), self.assertRaisesRegex(ValueError, "Duplicate runtime ownership"):
                checker.check_ownership(self.source, adapter(self.runtime + ["src/bench_util.cpp"], self.escape))
            with self.subTest(backend=adapter.__name__), self.assertRaisesRegex(ValueError, "Unexpected runtime sources"):
                checker.check_ownership(self.source, adapter(self.runtime + ["src/gentest_main.cpp"], self.escape))

    def test_empty_metadata_cannot_pass(self):
        for adapter in self.adapters():
            with self.subTest(backend=adapter.__name__), self.assertRaisesRegex(ValueError, "Unowned runtime sources"):
                checker.check_ownership(self.source, adapter([], []))
        with self.assertRaises(ValueError):
            checker.meson_sources([])
        with self.assertRaises(ValueError):
            checker.bazel_sources({"targets": [], "actions": []})
        with self.assertRaises(ValueError):
            checker.xmake_sources({"name": "another_target", "sourcebatches": []})
        with self.assertRaises(ValueError):
            checker.cmake_sources(self.source / "missing")

    def test_malformed_metadata_and_bazel_compile_inputs_fail(self):
        for metadata in ([], {}, {"name": "gentest_runtime", "sourcebatches": {}},
                         {"name": "gentest_runtime", "sourcebatches": [{"rulename": "c++.build", "sourcekind": "cxx"}]}):
            with self.subTest(metadata=metadata), self.assertRaises(ValueError):
                checker.xmake_sources(metadata)
        for metadata in ([], {}, {"targets": [{"id": "1", "label": "//:gentest_runtime"}], "actions": [
                {"targetId": "1", "mnemonic": "CppCompile", "arguments": ["clang++", "incidental.cpp"]}]}):
            with self.subTest(metadata=metadata), self.assertRaises(ValueError):
                checker.bazel_sources(metadata)
        with self.assertRaises(ValueError):
            checker.meson_sources([{"name": name} for name in checker.TARGETS])

    def test_bazel_cli_queries_configured_runtime_closure_before_reading_actions(self):
        metadata = {"targets": [{"id": "1", "label": "//:gentest_runtime"}], "actions": [
            {"targetId": "1", "mnemonic": "CppCompile", "arguments": ["clang++", "-c", path]}
            for path in self.runtime + self.escape]}
        with patch.object(sys, "argv", ["runtime_sources.py", "check", "--backend", "bazel", "--source", str(self.source),
                                       "--bazel-startup-arg=--output_user_root=chosen", "--bazel-arg=--cpu=chosen"]), \
                patch.object(checker, "query_json", return_value=metadata) as query:
            self.assertEqual(checker.main(), 0)
        command = query.call_args.args[0]
        self.assertEqual(command, ["bazel", "--output_user_root=chosen", "aquery", "--cpu=chosen",
            'mnemonic("CppCompile", deps(//:gentest_runtime))', "--output=jsonproto", "--include_commandline=true"])

    def test_cmake_requires_an_explicit_configuration_when_multiple_are_present(self):
        self.cmake(self.runtime, self.escape)
        path = self.source / "build/.cmake/api/v1/reply/model.json"
        model = json.loads(path.read_text())
        model["configurations"][0]["name"] = "Debug"
        model["configurations"].append(copy.deepcopy(model["configurations"][0]))
        model["configurations"][1]["name"] = "Release"
        path.write_text(json.dumps(model))
        with self.assertRaisesRegex(ValueError, "exactly one"):
            checker.cmake_sources(self.source / "build")
        self.assertEqual(checker.check_ownership(self.source, checker.cmake_sources(self.source / "build", "Release")), 3)

    def test_xmake_module_scan_and_builder_batches_are_not_compile_owners(self):
        metadata = {"name": "gentest_runtime", "sourcebatches": [
            {"rulename": rule, "sourcekind": "cxx", "sourcefiles": self.runtime + self.escape}
            for rule in ("c++.build", "c++.build.modules.scanner", "c++.build.modules.builder")]}
        self.assertEqual(checker.check_ownership(self.source, checker.xmake_sources(metadata)), 3)

    def test_bazel_pic_variants_keep_one_owner_per_configured_target(self):
        action = {"targetId": "1", "mnemonic": "CppCompile", "arguments": ["clang++", "-c", "src/benchmark.cpp"]}
        metadata = {"targets": [{"id": "1", "label": "//:gentest_runtime"}], "actions": [action, copy.deepcopy(action)]}
        metadata["actions"][1]["arguments"].append("-fPIC")
        self.assertEqual(checker.bazel_sources(metadata), [("//:gentest_runtime", "src/benchmark.cpp")])

    def test_symlinked_source_paths_preserve_physical_ownership(self):
        alias = self.source / "alias.cpp"
        try:
            alias.symlink_to(self.source / "src/benchmark.cpp")
        except OSError as error:
            self.skipTest(f"Host does not permit symlinks: {error}")
        with self.assertRaisesRegex(ValueError, "Duplicate runtime ownership"):
            checker.check_ownership(self.source, [("runtime", path) for path in self.runtime + self.escape + [str(alias)]])

    def test_real_cmake_configure_answers_a_query_created_in_the_same_run(self):
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake, "CMake is required for native metadata validation")
        (self.source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.31)\nproject(ownership LANGUAGES CXX)\n'
            'cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2)\n'
            'add_library(gentest_bench_escape STATIC src/bench_util.cpp)\n'
            'add_library(gentest_runtime STATIC src/benchmark.cpp src/runner_impl.cpp)\n'
            'target_link_libraries(gentest_runtime PRIVATE gentest_bench_escape)\n'
            'add_library(test_only STATIC src/gentest_main.cpp)\n')
        subprocess.run([cmake, "-S", str(self.source), "-B", str(self.source / "configured")],
                       check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(checker.check_ownership(self.source, checker.cmake_sources(self.source / "configured")), 3)


if __name__ == "__main__":
    unittest.main()
