from __future__ import annotations

import collections
import hashlib
import io
import json
import math
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
import sys

sys.path.insert(0, str(ROOT / "scripts"))

import bench_compile_campaign as campaign  # noqa: E402
import verify_codegen_parallel as verifier  # noqa: E402
from compile_bench_common import (  # noqa: E402
    CodegenCommandError,
    alternating_order,
    codegen_argument,
    codegen_job_values,
    median_mad,
    parse_codegen_jobs,
    rewrite_codegen_jobs,
    rewrite_ninja_codegen_commands,
    temporary_ninja_codegen_commands,
)
from bench_compile_campaign import (  # noqa: E402
    checkout_state,
    parse_codegen_caps,
    require_clean_checkout,
    require_fresh_output,
    resolve_cache_tool,
    rewrite_compdb,
)


class CodegenCommandRewriteTests(unittest.TestCase):
    def test_rewrite_stays_inside_codegen_chain_segment_and_quotes_paths(self) -> None:
        command = (
            "cd '/tmp/build with spaces' && cmake -E make_directory out && "
            "'/tmp/tool path/gentest_codegen' --jobs=1 --tu-out-dir '/tmp/out dir' && "
            "cmake --build . --jobs=99"
        )
        rewritten, metadata = rewrite_codegen_jobs(command, "4")
        self.assertEqual(metadata["effective"], 4)
        self.assertEqual(metadata["rewritten_tokens"], 1)
        self.assertEqual(codegen_job_values(rewritten), [4])
        self.assertIn("--jobs=99", rewritten)
        self.assertEqual(codegen_argument(rewritten, "--tu-out-dir"), "/tmp/out dir")

    def test_rewrites_each_codegen_command_but_not_other_programs(self) -> None:
        command = "gentest_codegen --jobs=1 input && gentest_codegen --jobs=auto input2 && tool --jobs=7"
        rewritten, metadata = rewrite_codegen_jobs(command, "2")
        self.assertEqual(codegen_job_values(rewritten), [2, 2])
        self.assertEqual(metadata["rewritten_tokens"], 2)
        self.assertTrue(rewritten.endswith("tool --jobs=7"))

    def test_windows_command_rewrite_preserves_cmd_quoting_and_backslashes(self) -> None:
        command = (
            'cmd.exe /C "cd /D C:\\build && '
            '"C:\\Program Files\\LLVM\\bin\\gentest_codegen.exe" --jobs=1 '
            '--tu-out-dir "C:\\generated output" && cmake --build . --jobs=9"'
        )
        rewritten, metadata = rewrite_codegen_jobs(command, "auto")
        self.assertEqual(metadata["effective"], 0)
        self.assertEqual(codegen_job_values(rewritten), [0])
        self.assertEqual(codegen_argument(rewritten, "--tu-out-dir"), r"C:\generated output")
        self.assertIn('"C:\\Program Files\\LLVM\\bin\\gentest_codegen.exe"', rewritten)
        self.assertIn("cmake --build . --jobs=9", rewritten)

    def test_missing_or_malformed_cap_is_not_silently_accepted(self) -> None:
        with self.assertRaisesRegex(CodegenCommandError, "no --jobs"):
            rewrite_codegen_jobs("gentest_codegen --tu-out-dir output", 1)
        with self.assertRaisesRegex(CodegenCommandError, "non-negative"):
            rewrite_codegen_jobs("gentest_codegen --jobs=lots", 1)
        with self.assertRaisesRegex(CodegenCommandError, "no gentest_codegen"):
            rewrite_codegen_jobs("tool --jobs=1", 1)

    def test_auto_and_bad_requested_values(self) -> None:
        self.assertEqual(parse_codegen_jobs("auto"), 0)
        self.assertEqual(parse_codegen_jobs("0"), 0)
        with self.assertRaises(CodegenCommandError):
            parse_codegen_jobs("-1")

    def test_build_ninja_rewrite_requires_effective_command(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            build_ninja = Path(temporary_directory) / "build.ninja"
            build_ninja.write_text(
                "  COMMAND = gentest_codegen validate-artifact-manifest --manifest manifest.json\n"
                "  COMMAND = cd /tmp && gentest_codegen --jobs=1 source && cmake --build . --jobs=9\n",
                encoding="utf-8",
            )
            metadata = rewrite_ninja_codegen_commands(build_ninja, "auto")
            self.assertEqual(metadata[0]["effective"], 0)
            rewritten = build_ninja.read_text(encoding="utf-8")
            self.assertEqual(codegen_job_values(rewritten.split("=", 1)[1]), [0])
            self.assertIn("--jobs=9", rewritten)
        with tempfile.TemporaryDirectory() as temporary_directory:
            build_ninja = Path(temporary_directory) / "build.ninja"
            build_ninja.write_text("COMMAND = gentest_codegen --tu-out-dir generated\n", encoding="utf-8")
            with self.assertRaises(CodegenCommandError):
                rewrite_ninja_codegen_commands(build_ninja, 1)

    def test_temporary_ninja_rewrite_restores_bytes_and_timestamp_after_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            build_ninja = Path(temporary_directory) / "build.ninja"
            original = b"  COMMAND = cd /tmp && gentest_codegen --jobs=1 source\n"
            build_ninja.write_bytes(original)
            original_mtime = build_ninja.stat().st_mtime_ns
            with self.assertRaisesRegex(RuntimeError, "sentinel"):
                with temporary_ninja_codegen_commands(build_ninja, 8):
                    self.assertEqual(codegen_job_values(build_ninja.read_text().split("=", 1)[1]), [8])
                    raise RuntimeError("sentinel")
            self.assertEqual(build_ninja.read_bytes(), original)
            self.assertEqual(build_ninja.stat().st_mtime_ns, original_mtime)


class StatisticsAndOrderTests(unittest.TestCase):
    def test_median_and_mad_retain_raw_seven_samples(self) -> None:
        samples = [1.0, 1.1, 0.9, 1.0, 1.2, 0.8, 1.0]
        stats = median_mad(samples)
        self.assertEqual(stats["samples_s"], samples)
        self.assertEqual(stats["median_s"], 1.0)
        self.assertAlmostEqual(float(stats["mad_s"]), 0.1)
        for invalid in ([math.nan], [math.inf], [-0.1]):
            with self.subTest(invalid=invalid), self.assertRaisesRegex(ValueError, "finite non-negative"):
                median_mad(invalid)

    def test_order_alternates_for_comparison_and_cap_sweeps(self) -> None:
        self.assertEqual(alternating_order(["A", "B"], 4), [["A", "B"], ["B", "A"], ["A", "B"], ["B", "A"]])
        self.assertEqual(alternating_order(["1", "2", "auto"], 3)[1], ["auto", "2", "1"])
        with self.assertRaises(ValueError):
            alternating_order([], 1)


class CampaignContractTests(unittest.TestCase):
    def test_provenance_hashes_file_tools(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            tool = Path(temporary_directory) / "cmake"
            tool.write_bytes(b"campaign-tool")
            with mock.patch.object(campaign, "run_output", return_value="cmake version"):
                provenance = campaign.version(tool)
        self.assertEqual(provenance["sha256"], hashlib.sha256(b"campaign-tool").hexdigest())
        self.assertEqual(provenance["version"], "cmake version")

    def test_parallel_verifier_detects_new_output_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            build_dir = Path(temporary_directory)
            (build_dir / "build.ninja").write_text("# fixture\n", encoding="utf-8")
            baseline_output = build_dir / "tu_0000.gentest.h"
            extra_output = build_dir / "tu_0001.gentest.h"
            baseline_output.write_text("baseline\n", encoding="utf-8")
            extra_output.write_text("parallel-only\n", encoding="utf-8")
            with (
                mock.patch.object(sys, "argv", ["verify_codegen_parallel.py", "--build-dir", str(build_dir), "--repeats", "1"]),
                mock.patch.object(verifier, "parse_codegen_commands", return_value={"gentest_codegen_parallel_bench_obj": "codegen"}),
                mock.patch.object(verifier, "run_codegen", return_value={"effective": 1}),
                mock.patch.object(
                    verifier,
                    "collect_outputs",
                    side_effect=[[baseline_output], [baseline_output], [baseline_output, extra_output]],
                ),
                mock.patch.object(verifier, "remove_outputs"),
                mock.patch.object(sys, "stdout", io.StringIO()),
                mock.patch.object(sys, "stderr", io.StringIO()),
            ):
                self.assertEqual(verifier.main(), 1)

    def test_parallel_verifier_detects_missing_reemission_after_removing_baseline(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            build_dir = Path(temporary_directory)
            (build_dir / "build.ninja").write_text("# fixture\n", encoding="utf-8")
            output_dir = build_dir / "generated"
            output_dir.mkdir()
            baseline_output = output_dir / "tu_0000.gentest.h"
            baseline_output.write_text("serial baseline\n", encoding="utf-8")
            command = f"gentest_codegen --jobs=1 --tu-out-dir {output_dir}"
            invocations = 0

            def fake_run_codegen(_command, _jobs):  # noqa: ANN001, ANN202
                nonlocal invocations
                invocations += 1
                if invocations == 1:
                    baseline_output.write_text("serial baseline\n", encoding="utf-8")
                return {"effective": 1}

            with (
                mock.patch.object(sys, "argv", ["verify_codegen_parallel.py", "--build-dir", str(build_dir), "--repeats", "1"]),
                mock.patch.object(verifier, "parse_codegen_commands", return_value={"gentest_codegen_parallel_bench_obj": command}),
                mock.patch.object(verifier, "run_codegen", side_effect=fake_run_codegen),
                mock.patch.object(sys, "stdout", io.StringIO()),
                mock.patch.object(sys, "stderr", io.StringIO()),
            ):
                self.assertEqual(verifier.main(), 1)
                self.assertFalse(baseline_output.exists())

    def test_ninja_log_snapshot_survives_compaction(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            build_dir = Path(temporary_directory)
            ninja_log = build_dir / ".ninja_log"
            ninja_log.write_text(
                "# ninja log v5\n1\t2\t3\ta.o\thash-a\n1\t2\t3\tb.o\thash-b\n1\t2\t3\tc.o\thash-c\n",
                encoding="utf-8",
            )
            before = campaign.ninja_log_snapshot(build_dir)
            ninja_log.write_text("# ninja log v5\n4\t5\t6\ta.o\thash-a\n", encoding="utf-8")
            summary = campaign.summarize_new_ninja_edges(build_dir, before)
        self.assertEqual(summary["unique_edges"], 1)
        self.assertEqual(summary["categories"], {"compile": 1})

    def test_ninja_log_snapshot_counts_repeated_identical_edges(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            build_dir = Path(temporary_directory)
            ninja_log = build_dir / ".ninja_log"
            record = "1\t2\t3\ttu_case.gentest.h\tcodegen-hash\n"
            ninja_log.write_text(f"# ninja log v5\n{record}", encoding="utf-8")
            before = campaign.ninja_log_snapshot(build_dir)
            ninja_log.write_text(f"# ninja log v5\n{record}{record}", encoding="utf-8")
            summary = campaign.summarize_new_ninja_edges(build_dir, before)
        self.assertEqual(summary["unique_edges"], 1)
        self.assertEqual(summary["categories"], {"codegen": 1})

    def test_build_recompacts_log_before_snapshot_and_timing(self) -> None:
        events: list[str] = []

        def fake_recompact(*_args, **_kwargs):
            events.append("recompact")

        def fake_snapshot(*_args, **_kwargs):
            events.append("snapshot")
            return collections.Counter()

        def fake_run(*_args, **_kwargs):
            events.append("build")

        with (
            mock.patch.object(campaign, "recompact_ninja_log", side_effect=fake_recompact),
            mock.patch.object(campaign, "ninja_log_snapshot", side_effect=fake_snapshot),
            mock.patch.object(campaign, "run", side_effect=fake_run),
        ):
            campaign.build_target(Path("/source"), Path("/build"), ["fixture"], 1, {})
        self.assertEqual(events, ["recompact", "snapshot", "build", "snapshot"])

    def test_cache_mode_and_dirty_checkout_validation_are_explicit(self) -> None:
        self.assertIsNone(resolve_cache_tool("off", lambda _: None))
        self.assertEqual(resolve_cache_tool("ccache", lambda name: f"/tools/{name}"), "/tools/ccache")
        with self.assertRaisesRegex(RuntimeError, "not on PATH"):
            resolve_cache_tool("sccache", lambda _: None)
        require_clean_checkout(False, False)
        require_clean_checkout(True, True)
        with self.assertRaisesRegex(RuntimeError, "checkout is dirty"):
            require_clean_checkout(True, False)

    def test_selected_cache_clears_its_inherited_disable_flag(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            with (
                mock.patch.dict(
                    os.environ,
                    {
                        "CCACHE_DISABLE": "1",
                        "SCCACHE_DISABLE": "1",
                        "CMAKE_C_COMPILER_LAUNCHER": "ambient-c-launcher",
                        "CMAKE_CXX_COMPILER_LAUNCHER": "ambient-cxx-launcher",
                    },
                ),
                mock.patch.object(campaign, "resolve_cache_tool", return_value="/bin/true"),
                mock.patch.object(campaign, "run_output", return_value="empty stats"),
                mock.patch.object(campaign, "version", return_value={"path": "/bin/true"}),
            ):
                ccache_env, _ = campaign.cache_environment("ccache", Path(temporary_directory) / "ccache")
        self.assertNotIn("CCACHE_DISABLE", ccache_env)
        self.assertEqual(ccache_env["SCCACHE_DISABLE"], "1")
        self.assertNotIn("CMAKE_C_COMPILER_LAUNCHER", ccache_env)
        self.assertNotIn("CMAKE_CXX_COMPILER_LAUNCHER", ccache_env)

    @unittest.skipIf(os.name == "nt", "isolated sccache campaigns use Unix-domain sockets")
    def test_selected_sccache_clears_its_inherited_disable_flag(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            with (
                mock.patch.dict(
                    os.environ,
                    {
                        "SCCACHE_DISABLE": "1",
                        "CMAKE_C_COMPILER_LAUNCHER": "ambient-c-launcher",
                        "CMAKE_CXX_COMPILER_LAUNCHER": "ambient-cxx-launcher",
                    },
                ),
                mock.patch.object(campaign, "resolve_cache_tool", return_value="/bin/true"),
                mock.patch.object(campaign, "run_output", return_value="empty stats"),
                mock.patch.object(campaign, "version", return_value={"path": "/bin/true"}),
            ):
                sccache_env, _ = campaign.cache_environment("sccache", Path(temporary_directory) / "sccache")
        self.assertNotIn("SCCACHE_DISABLE", sccache_env)
        self.assertEqual(sccache_env["CCACHE_DISABLE"], "1")
        self.assertNotIn("CMAKE_C_COMPILER_LAUNCHER", sccache_env)
        self.assertNotIn("CMAKE_CXX_COMPILER_LAUNCHER", sccache_env)

    def test_cache_off_clears_launchers_in_environment_and_cmake_cache(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            with mock.patch.dict(
                os.environ,
                {
                    "CMAKE_C_COMPILER_LAUNCHER": "ambient-c-launcher",
                    "CMAKE_CXX_COMPILER_LAUNCHER": "ambient-cxx-launcher",
                },
            ):
                cache_env, _ = campaign.cache_environment("off", Path(temporary_directory))
        self.assertNotIn("CMAKE_C_COMPILER_LAUNCHER", cache_env)
        self.assertNotIn("CMAKE_CXX_COMPILER_LAUNCHER", cache_env)
        arguments = campaign.cmake_arguments("clang", "clang++", "Release", "off")
        self.assertIn("-DCMAKE_C_COMPILER_LAUNCHER=", arguments)
        self.assertIn("-DCMAKE_CXX_COMPILER_LAUNCHER=", arguments)

    @unittest.skipIf(os.name == "nt", "isolated sccache UDS is a POSIX campaign contract")
    def test_sccache_lifecycle_uses_one_isolated_endpoint(self) -> None:
        calls: list[tuple[list[str], dict[str, str]]] = []

        def fake_run_output(command, *, cwd=None, env=None):  # noqa: ANN001, ANN202
            del cwd
            calls.append((list(command), dict(env or {})))
            return "stats"

        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            with (
                mock.patch.dict(
                    os.environ,
                    {
                        "SCCACHE_SERVER_PORT": "4226",
                        "SCCACHE_SERVER_UDS": "/tmp/ambient-sccache.sock",
                        "SCCACHE_CONF": "/tmp/ambient-sccache.conf",
                    },
                ),
                mock.patch.object(campaign, "resolve_cache_tool", return_value="/tools/sccache"),
                mock.patch.object(campaign, "run_output", side_effect=fake_run_output),
                mock.patch.object(campaign, "version", return_value={"path": "/tools/sccache"}),
                mock.patch.object(campaign.shutil, "which", return_value="/tools/sccache"),
            ):
                cache_env, metadata = campaign.cache_environment("sccache", root)
                campaign.finish_cache_metadata("sccache", cache_env, root, metadata)

        self.assertEqual([command[1] for command, _ in calls], ["--start-server", "--show-stats", "--show-stats", "--stop-server"])
        endpoint = cache_env["SCCACHE_SERVER_UDS"]
        self.assertNotEqual(endpoint, "/tmp/ambient-sccache.sock")
        self.assertNotIn("SCCACHE_SERVER_PORT", cache_env)
        self.assertNotEqual(cache_env["SCCACHE_CONF"], "/tmp/ambient-sccache.conf")
        self.assertEqual(metadata["server_endpoint"], {"kind": "uds", "path": endpoint})
        for _, call_env in calls:
            self.assertEqual(call_env["SCCACHE_SERVER_UDS"], endpoint)
            self.assertEqual(call_env["SCCACHE_CONF"], cache_env["SCCACHE_CONF"])

    @unittest.skipIf(os.name == "nt", "isolated sccache UDS is a POSIX campaign contract")
    def test_sccache_long_endpoint_fallback_is_fresh_and_cleaned(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / ("deep" * 30)
            with (
                mock.patch.object(campaign, "resolve_cache_tool", return_value="/tools/sccache"),
                mock.patch.object(campaign, "run_output", return_value="stats"),
                mock.patch.object(campaign, "version", return_value={"path": "/tools/sccache"}),
                mock.patch.object(campaign.shutil, "which", return_value="/tools/sccache"),
            ):
                first_env, first_metadata = campaign.cache_environment("sccache", root)
                second_env, second_metadata = campaign.cache_environment("sccache", root)
                first_temporary_directory = Path(first_metadata["_server_temporary_directory"])
                second_temporary_directory = Path(second_metadata["_server_temporary_directory"])
                self.assertNotEqual(first_env["SCCACHE_SERVER_UDS"], second_env["SCCACHE_SERVER_UDS"])
                self.assertTrue(first_temporary_directory.is_dir())
                self.assertTrue(second_temporary_directory.is_dir())
                campaign.finish_cache_metadata("sccache", first_env, root, first_metadata)
                campaign.finish_cache_metadata("sccache", second_env, root, second_metadata)
            self.assertFalse(first_temporary_directory.exists())
            self.assertFalse(second_temporary_directory.exists())

    @unittest.skipIf(os.name == "nt", "isolated sccache UDS is a POSIX campaign contract")
    def test_sccache_failure_shutdown_removes_long_endpoint_fallback(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / ("deep" * 30)
            with (
                mock.patch.object(campaign, "resolve_cache_tool", return_value="/tools/sccache"),
                mock.patch.object(campaign, "run_output", return_value="stats"),
                mock.patch.object(campaign, "version", return_value={"path": "/tools/sccache"}),
            ):
                cache_env, metadata = campaign.cache_environment("sccache", root)
                temporary_endpoint = Path(metadata["_server_temporary_directory"])
                self.assertTrue(temporary_endpoint.is_dir())
                campaign.shutdown_sccache(cache_env, root, metadata, check=False)
            self.assertFalse(temporary_endpoint.exists())

    def test_extensionless_repository_executable_is_a_link_edge(self) -> None:
        self.assertEqual(campaign.classify(["tests/gentest_unit_tests"], ("gentest_unit_tests",)), "link_or_archive")
        self.assertEqual(campaign.classify(["tests/gentest_unit_tests"]), "other")

    def test_target_named_output_directory_is_not_a_link_edge(self) -> None:
        staging_output = "generated/campaign_eight_tu/compdb/compile_commands.checked"
        self.assertEqual(campaign.classify([staging_output], ("campaign_eight_tu",)), "other")
        self.assertEqual(campaign.classify(["/tmp/build/campaign_eight_tu"], ("campaign_eight_tu",)), "link_or_archive")

    def test_equivalent_compdb_rewrite_accepts_staging_or_codegen_without_downstream_work(self) -> None:
        staging_only = {"unique_edges": 1, "categories": {"other": 1}}
        campaign.validate_contract("equivalent-compdb-rewrite", staging_only, 8, True, True, True)
        codegen_only = {"unique_edges": 2, "categories": {"other": 1, "codegen": 1}}
        campaign.validate_contract("equivalent-compdb-rewrite", codegen_only, 8, True, True, True)
        with self.assertRaisesRegex(RuntimeError, "without downstream work"):
            campaign.validate_contract(
                "equivalent-compdb-rewrite",
                {"unique_edges": 3, "categories": {"other": 1, "codegen": 1, "link_or_archive": 1}},
                8,
                True,
                True,
                True,
            )
        with self.assertRaisesRegex(RuntimeError, "expected codegen edge"):
            campaign.validate_contract("unrelated-compdb-rewrite", staging_only, 8, True, True, True)

    def test_codegen_cap_validation_rejects_ambiguous_sweeps(self) -> None:
        self.assertEqual(parse_codegen_caps("1,auto,4"), [("1", 1), ("auto", 0), ("4", 4)])
        with self.assertRaisesRegex(ValueError, "repeat a cap label"):
            parse_codegen_caps("1,1")
        with self.assertRaisesRegex(ValueError, "repeat an effective cap"):
            parse_codegen_caps("auto,0")
        with self.assertRaises(ValueError):
            parse_codegen_caps("")

    def test_output_directory_must_be_empty(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / "result"
            require_fresh_output(output)
            output.mkdir()
            require_fresh_output(output)
            (output / "partial-build").mkdir()
            with self.assertRaisesRegex(RuntimeError, "not empty"):
                require_fresh_output(output)

    def test_compdb_rewrites_distinguish_equivalent_and_unrelated_changes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            compdb = Path(temporary_directory) / "compile_commands.json"
            original = [{"directory": temporary_directory, "file": "case.cpp", "command": "c++ -c case.cpp"}]
            compdb.write_text(json.dumps(original), encoding="utf-8")
            rewrite_compdb(compdb, add_unrelated=False)
            self.assertEqual(json.loads(compdb.read_text()), original)
            rewrite_compdb(compdb, add_unrelated=True)
            rewritten = json.loads(compdb.read_text())
            self.assertEqual(rewritten[:-1], original)
            self.assertTrue(rewritten[-1]["file"].endswith("__gentest_unrelated_compdb_entry__.cpp"))

    def test_reconfigure_preserves_cmake_compdb_timestamp_and_detects_staging(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            source = root / "source"
            build = root / "build"
            source.mkdir()
            build.mkdir()
            compdb = build / "compile_commands.json"
            compdb.write_text("[]\n", encoding="utf-8")
            before = compdb.stat().st_mtime_ns

            def fake_run(*_args, **_kwargs):
                os.utime(compdb, ns=(before + 1_000_000, before + 1_000_000))

            with mock.patch.object(campaign, "run", side_effect=fake_run):
                campaign.reconfigure(source, build, [], {})
            self.assertGreater(compdb.stat().st_mtime_ns, before)

            (build / "build.ninja").write_text(
                "DESC = Staging compile commands for gentest target fixture\n"
                "COMMAND = cmake -E copy_if_different compile_commands.json generated/compdb/compile_commands.json\n",
                encoding="utf-8",
            )
            self.assertTrue(campaign.uses_content_stable_compdb_stage(build))

    def test_mutation_scenarios_restore_identical_inputs_and_settle(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            source = root / "source"
            build = root / "build"
            source.mkdir()
            build.mkdir()
            originals = {
                source / "case_00.cpp": b"void case_00() {}\n",
                source / "private.hpp": b"#pragma once\n",
                source / "shared.hpp": b"#pragma once\n",
                build / "compile_commands.json": b'[{"directory":".","file":"case_00.cpp","command":"c++ -c case_00.cpp"}]\n',
            }
            for path, contents in originals.items():
                path.write_bytes(contents)

            def fake_build(*_args, **_kwargs):
                if b"source-edit" in (source / "case_00.cpp").read_bytes():
                    return 0.1, {"unique_edges": 2, "categories": {"codegen": 1, "generated_tu_compile": 1}}
                if b"private-header-edit" in (source / "private.hpp").read_bytes():
                    return 0.1, {"unique_edges": 2, "categories": {"codegen": 1, "generated_tu_compile": 1}}
                if b"shared-header-edit" in (source / "shared.hpp").read_bytes():
                    return 0.1, {"unique_edges": 9, "categories": {"codegen": 1, "generated_tu_compile": 8}}
                compdb = json.loads((build / "compile_commands.json").read_text())
                if len(compdb) > 1 or (build / "compile_commands.json").read_bytes() != originals[build / "compile_commands.json"]:
                    return 0.1, {"unique_edges": 1, "categories": {"codegen": 1}}
                return 0.01, {"unique_edges": 0, "categories": {}}

            scenarios = (
                "source-edit",
                "private-header-edit",
                "shared-header-edit",
                "equivalent-compdb-rewrite",
                "unrelated-compdb-rewrite",
            )
            with mock.patch.object(campaign, "build_target", side_effect=fake_build):
                results = campaign.run_scenarios(
                    source, build, [], ["fixture"], scenarios, 1, 0, 1, {}, True, 8, True, True
                )
            for path, contents in originals.items():
                self.assertEqual(path.read_bytes(), contents)
            for scenario in scenarios:
                self.assertEqual(results[scenario]["settling_profiles"], [{"unique_edges": 0, "categories": {}}])
            self.assertEqual(results["source-edit"]["restoration_profiles"], [])
            self.assertEqual(
                results["equivalent-compdb-rewrite"]["restoration_profiles"],
                [{"unique_edges": 0, "categories": {}}],
            )
            self.assertEqual(
                results["unrelated-compdb-rewrite"]["restoration_profiles"],
                [{"unique_edges": 0, "categories": {}}],
            )

    def test_repository_mutation_settles_to_codegen_only_and_rejects_relink(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            source = root / "source"
            build = root / "build"
            source.mkdir()
            build.mkdir()
            (build / "compile_commands.json").write_text(
                '[{"directory":".","file":"case.cpp","command":"c++ -c case.cpp"}]\n', encoding="utf-8"
            )
            codegen_only = (0.1, {"unique_edges": 1, "categories": {"codegen": 1}})
            with mock.patch.object(campaign, "build_target", side_effect=[codegen_only, codegen_only, codegen_only]):
                result = campaign.run_scenarios(
                    source, build, [], ["gentest_unit_tests"], ("equivalent-compdb-rewrite",), 1, 0, 1, {}, False, 0, True, False
                )
            self.assertEqual(result["equivalent-compdb-rewrite"]["settling_profiles"], [codegen_only[1]])
            self.assertEqual(result["equivalent-compdb-rewrite"]["restoration_profiles"], [codegen_only[1]])

            relink = (0.1, {"unique_edges": 2, "categories": {"codegen": 1, "link_or_archive": 1}})
            with (
                mock.patch.object(campaign, "build_target", side_effect=[codegen_only, relink]),
                self.assertRaisesRegex(RuntimeError, "restoration reran downstream"),
            ):
                campaign.run_scenarios(
                    source, build, [], ["gentest_unit_tests"], ("equivalent-compdb-rewrite",), 1, 0, 1, {}, False, 0, True, False
                )

            with (
                mock.patch.object(campaign, "build_target", side_effect=[codegen_only, codegen_only, relink]),
                self.assertRaisesRegex(RuntimeError, "persistent codegen-only edge"),
            ):
                campaign.run_scenarios(
                    source, build, [], ["gentest_unit_tests"], ("equivalent-compdb-rewrite",), 1, 0, 1, {}, False, 0, True, False
                )

    def test_untracked_noise_is_recorded_but_does_not_block_worktree_isolation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo = Path(temporary_directory)
            subprocess.run(["git", "init", "--initial-branch=master"], cwd=repo, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            subprocess.run(["git", "config", "user.name", "Benchmark Test"], cwd=repo, check=True)
            subprocess.run(["git", "config", "user.email", "benchmark@example.invalid"], cwd=repo, check=True)
            (repo / "tracked.txt").write_text("base\n", encoding="utf-8")
            subprocess.run(["git", "add", "tracked.txt"], cwd=repo, check=True)
            subprocess.run(["git", "commit", "-m", "base"], cwd=repo, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            (repo / "untracked.txt").write_text("noise\n", encoding="utf-8")
            state = checkout_state(repo)
            self.assertTrue(state["dirty"])
            self.assertFalse(state["tracked_dirty"])
            require_clean_checkout(bool(state["tracked_dirty"]), False)
            (repo / "tracked.txt").write_text("changed\n", encoding="utf-8")
            state = checkout_state(repo)
            self.assertTrue(state["tracked_dirty"])
            with self.assertRaises(RuntimeError):
                require_clean_checkout(bool(state["tracked_dirty"]), False)

    def test_repository_build_roots_are_fresh_and_source_local(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            source = Path(temporary_directory) / "source"
            source.mkdir()
            first = campaign.fresh_repository_build_root(source)
            second = campaign.fresh_repository_build_root(source)
            self.assertNotEqual(first, second)
            self.assertEqual(first.parent, source / "build")
            self.assertEqual(second.parent, source / "build")
            self.assertTrue(first.is_dir())
            self.assertTrue(second.is_dir())

    def test_manual_workflow_and_documentation_keep_campaign_contract(self) -> None:
        workflow = (ROOT / ".github/workflows/compile_benchmark_campaign.yml").read_text(encoding="utf-8")
        document = (ROOT / "docs/compile_benchmark_campaign.md").read_text(encoding="utf-8")
        script = (ROOT / "scripts/bench_compile_campaign.py").read_text(encoding="utf-8")
        self.assertIn("workflow_dispatch", workflow)
        self.assertIn("clang++-22", workflow)
        self.assertIn("g++-16", workflow)
        self.assertIn("upload-artifact", workflow)
        self.assertIn("compile-campaign-${{ matrix.cc }}/result.json", workflow)
        self.assertIn("compile-campaign-${{ matrix.cc }}/summary.md", workflow)
        self.assertNotIn("path: ${{ runner.temp }}/compile-campaign-${{ matrix.cc }}\n", workflow)
        self.assertIn("apt.llvm.org", workflow)
        self.assertIn("gcc-16", workflow)
        self.assertIn("9edd3c826eadb31714f6462b5264cc1793bb535b", document)
        self.assertIn("samples_s", document)
        self.assertIn("--cache", script)
        self.assertIn("DEFAULT_SCENARIOS", script)


if __name__ == "__main__":
    unittest.main()
