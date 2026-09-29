"""Contracts for the resolved commands shared by local validation and CI."""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from validation_suites import suite_commands


class SuiteContracts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.input_work = Path(self.temp.name)
        # macOS spells this temporary directory as /var but resolves it under /private/var.
        self.work = self.input_work.resolve()
        self.codegen = self.work / "installed-codegen"

    def commands(self, suite):
        return suite_commands(suite, ROOT, self.input_work, codegen=self.codegen, cc="selected-clang", cxx="selected-clang++")

    def test_bazel_consumers_are_built_and_executed_with_the_selected_toolchain(self):
        commands = self.commands("bazel")
        builds = [command for command in commands if len(command.args) > 2 and command.args[2] in ("build", "test")]
        self.assertEqual(len(builds), 3)
        self.assertIn("//:codegen_check_invalid", builds[0].args)
        for command in builds:
            self.assertIn("--repo_contents_cache=", command.args)
            self.assertIn("--repo_env=GENTEST_BAZEL_LOCAL_CLANG", command.args)
            self.assertEqual(command.env["GENTEST_BAZEL_LOCAL_CLANG"], "selected-clang++")
            self.assertEqual(command.env["CC"], "selected-clang")
            self.assertEqual(command.env["CXX"], "selected-clang++")
            for key in ("CC", "CXX"):
                self.assertIn("--action_env=" + key, command.args)
                self.assertIn("--host_action_env=" + key, command.args)
            for obsolete in ("GENTEST_CODEGEN_HOST_CLANG", "GENTEST_CODEGEN_RESOURCE_DIR"):
                self.assertFalse(any(arg.split("=", 1)[-1] == obsolete for arg in command.args))
        self.assertIn("--experimental_cpp_modules", builds[2].args)
        self.assertIn("//:gentest_consumer_textual_bazel", builds[1].args)
        self.assertIn("//:gentest_consumer_module_bazel", builds[2].args)
        self.assert_consumers(commands, "bazel")
        self.assertTrue(commands[-1].args[-1].endswith("CheckBazelBzlmodConsumer.cmake"))

    def assert_consumers(self, commands, backend):
        expected = [["--list"], ["--run=consumer/consumer/module_test", "--kind=test"],
                    ["--run=consumer/consumer/module_mock", "--kind=test"],
                    ["--run=consumer/consumer/log_sink", "--kind=test"],
                    ["--run=consumer/consumer/module_bench", "--kind=bench"],
                    ["--run=consumer/consumer/module_jitter", "--kind=jitter"]]
        for style in ("textual", "module"):
            target = f"gentest_consumer_{style}_{backend}"
            prefix = [str(self.work / "bazel-bin" / target)] if backend == "bazel" else ["xmake", "r", "-y", target]
            actual = [command.args[len(prefix):] for command in commands if command.args[:len(prefix)] == prefix]
            self.assertEqual(actual, expected)

    def test_xmake_consumers_run_through_the_configured_target_and_codegen(self):
        commands = self.commands("xmake")
        self.assertEqual(commands[0].args[:6], ["xmake", "f", "-c", "-y", "-m", "release"])
        self.assertIn(str(self.work / "build"), commands[0].args)
        self.assert_consumers(commands, "xmake")
        for command in commands:
            self.assertEqual(command.env["GENTEST_CODEGEN"], str(self.codegen))
            self.assertEqual(command.env["GENTEST_CODEGEN_HOST_CLANG"], "selected-clang++")
        for target in ("unit", "integration", "fixtures", "skiponly"):
            self.assertIn(["xmake", "r", "-y", f"gentest_{target}_xmake"], [c.args for c in commands])
        self.assertTrue(commands[-1].args[-1].endswith("CheckXmakeXrepoConsumer.cmake"))

    def test_meson_keeps_repo_tests_and_downstream_wrap_acceptance(self):
        commands = self.commands("meson")
        self.assertEqual(len(commands), 4)
        self.assertIn("-Dcodegen_path=" + str(self.codegen), commands[0].args)
        self.assertIn("-Dcodegen_host_clang=selected-clang++", commands[0].args)
        self.assertEqual(commands[1].args[:2], ["meson", "compile"])
        self.assertEqual(commands[2].args[:2], ["meson", "test"])
        self.assertTrue(commands[3].args[-1].endswith("CheckMesonWrapConsumer.cmake"))


if __name__ == "__main__":
    unittest.main()
