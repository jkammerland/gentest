"""Exercise CTest package lifecycle with small, real installed C libraries."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]


class PackageFixtureTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.source = self.root / "source"
        self.source.mkdir()
        self.consumer = self.source / "tests/consumer"
        self.consumer.mkdir(parents=True)
        self.driver = self.root / "driver"
        self.driver.mkdir()
        self.build = self.root / "build"
        self.cmake = shutil.which("cmake")
        self.ctest = shutil.which("ctest")
        self.assertTrue(self.cmake and self.ctest, "CMake and CTest are required")
        self.write_answer(42)
        (self.source / "tool.c").write_text("int main(void) { return 0; }\n")
        (self.source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.25)
project(sample LANGUAGES C)
file(APPEND "${CMAKE_SOURCE_DIR}/events.txt" "configure:${CMAKE_BUILD_TYPE}\n")
set(GENTEST_FMT_VERSION 1.0.0 CACHE STRING "fixture dependency version")
add_library(sample STATIC answer.c)
add_executable(gentest_codegen tool.c)
install(TARGETS sample EXPORT sampleTargets ARCHIVE DESTINATION lib)
install(TARGETS gentest_codegen RUNTIME DESTINATION bin)
install(EXPORT sampleTargets NAMESPACE sample:: DESTINATION lib/cmake/sample)
file(WRITE "${CMAKE_BINARY_DIR}/sampleConfig.cmake"
  "# find_dependency(fmt 1.0.0 EXACT CONFIG REQUIRED)\ninclude(\\"\\${CMAKE_CURRENT_LIST_DIR}/sampleTargets.cmake\\")\n")
install(FILES "${CMAKE_BINARY_DIR}/sampleConfig.cmake" DESTINATION lib/cmake/sample)
install(CODE "file(APPEND \\"${CMAKE_SOURCE_DIR}/events.txt\\" \\"install:${CMAKE_BUILD_TYPE}\\n\\")")
install(CODE "file(SHA256 \\"\\${CMAKE_INSTALL_PREFIX}/bin/gentest_codegen${CMAKE_EXECUTABLE_SUFFIX}\\" digest)
file(WRITE \\"\\${CMAKE_INSTALL_PREFIX}/codegen.digest\\" \\"\\${digest}\\")")
''')
        (self.consumer / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.25)
project(consumer LANGUAGES C)
find_package(sample CONFIG REQUIRED)
get_filename_component(prefix "${sample_DIR}/../../.." ABSOLUTE)
file(READ "${prefix}/codegen.digest" expected)
file(SHA256 "${prefix}/bin/gentest_codegen${CMAKE_EXECUTABLE_SUFFIX}" actual)
if(NOT actual STREQUAL expected)
  message(FATAL_ERROR "A consumer changed the shared installed codegen")
endif()
add_executable(gentest_consumer main.c)
target_link_libraries(gentest_consumer PRIVATE sample::sample)
''')

    def write_answer(self, answer):
        (self.source / "answer.c").write_text(f"int answer(void) {{ return {answer}; }}\n")
        (self.consumer / "main.c").write_text(
            f"int answer(void);\nint main(void) {{ return answer() != {answer}; }}\n")

    def configure(self, *, extra="", generator="Ninja"):
        (self.driver / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.25)
project(driver LANGUAGES NONE)
include(CTest)
include("{ROOT.as_posix()}/tests/cmake/GentestCmakeFixtureTests.cmake")
set(_gentest_codegen_prog "${{CMAKE_COMMAND}}")
foreach(mode IN ITEMS main_only runtime_only double)
  _gentest_add_cmake_helper_test("consumer_${{mode}}" "{self.source.as_posix()}"
    "{ROOT.as_posix()}/tests/cmake/scripts/CheckPackageConsumer.cmake" package
    -DPACKAGE_NAME=sample -DPACKAGE_TEST_USE_MODULES=OFF -DBUILD_TYPE=Release -DBUILD_CONFIG=Release
    -DCONSUMER_LINK_MODE=${{mode}} -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON)
endforeach()
_gentest_add_cmake_helper_test(consumer_native "{self.source.as_posix()}"
  "{ROOT.as_posix()}/tests/cmake/scripts/CheckPackageConsumer.cmake" package
  -DPACKAGE_NAME=sample -DPACKAGE_TEST_USE_MODULES=OFF -DBUILD_TYPE=Release -DBUILD_CONFIG=Release
  -DCONSUMER_LINK_MODE=double -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=OFF)
{extra}
''')
        self.run_command([self.cmake, "-G", generator, "-S", str(self.driver), "-B", str(self.build)])

    def run_command(self, command, *, succeeds=True):
        result = subprocess.run(command, text=True, capture_output=True)
        self.assertEqual(result.returncode == 0, succeeds, result.stdout + result.stderr)
        return result

    def run_ctest(self, selection="^consumer_", *, succeeds=True):
        return self.run_command([self.ctest, "--test-dir", str(self.build), "--output-on-failure",
                                 "-C", "RelWithDebInfo" if os.name == "nt" else "Release",
                                 "--parallel", "4", "--no-tests=error", "-R", selection,
                                 "--output-junit", str(self.build / "results.xml")], succeeds=succeeds)

    def assert_events(self, configurations):
        events = (self.source / "events.txt").read_text().splitlines()
        self.assertCountEqual(events, [f"{phase}:{config}" for config in configurations for phase in ("configure", "install")])
        self.assertEqual(list((self.build / "package_fixtures").glob("pkg_*")), [], "CTest cleanup did not remove the producer")

    def test_parallel_consumers_share_one_immutable_producer(self):
        self.configure()
        inventory = json.loads(self.run_command([self.ctest, "--test-dir", str(self.build), "--show-only=json-v1"]).stdout)
        self.assertEqual(sum(test["name"].endswith("_setup") for test in inventory["tests"]), 1)
        self.run_ctest()
        self.assert_events(["RelWithDebInfo" if os.name == "nt" else "Release"])

    def test_single_consumer_selection_adds_setup_and_cleanup_and_rebuilds_changed_source(self):
        self.configure()
        first = self.run_ctest("^consumer_main_only$")
        self.assertIn("_setup", first.stdout)
        self.assertIn("_cleanup", first.stdout)
        self.write_answer(73)
        self.run_ctest("^consumer_main_only$")
        config = "RelWithDebInfo" if os.name == "nt" else "Release"
        self.assert_events([config, config])

    def test_multi_config_builds_only_the_requested_configuration(self):
        self.configure(generator="Ninja Multi-Config")
        self.run_ctest("^consumer_main_only$")
        self.assert_events(["RelWithDebInfo" if os.name == "nt" else "Release"])

    def test_different_actual_configurations_have_distinct_producers(self):
        self.configure(extra=f'''
_gentest_add_cmake_helper_test(consumer_minsizerel "{self.source.as_posix()}"
  "{ROOT.as_posix()}/tests/cmake/scripts/CheckPackageConsumer.cmake" package
  -DPACKAGE_NAME=sample -DPACKAGE_TEST_USE_MODULES=OFF -DBUILD_TYPE=MinSizeRel
  -DCONSUMER_LINK_MODE=double -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=OFF)
''')
        self.run_ctest()
        self.assert_events(["RelWithDebInfo"] if os.name == "nt" else ["Release", "MinSizeRel"])

    def test_producer_failure_blocks_consumers_and_still_cleans_up(self):
        self.configure()
        with (self.source / "CMakeLists.txt").open("a") as stream:
            stream.write('\nmessage(FATAL_ERROR "producer regression")\n')
        self.run_ctest(succeeds=False)
        cases = ET.parse(self.build / "results.xml").getroot().findall("testcase")
        consumers = [case for case in cases if case.attrib["name"].startswith("consumer_")]
        self.assertEqual(len(consumers), 4)
        self.assertTrue(all(case.find("skipped") is not None for case in consumers))
        self.assertEqual(list((self.build / "package_fixtures").glob("pkg_*")), [])

    def test_consumer_failure_preserves_failure_and_runs_cleanup(self):
        self.configure()
        (self.consumer / "main.c").write_text('#error consumer regression\n')
        self.run_ctest("^consumer_main_only$", succeeds=False)
        cases = ET.parse(self.build / "results.xml").getroot().findall("testcase")
        consumer = next(case for case in cases if case.attrib["name"] == "consumer_main_only")
        self.assertIsNotNone(consumer.find("failure"))
        self.assertEqual(list((self.build / "package_fixtures").glob("pkg_*")), [])


if __name__ == "__main__":
    unittest.main()
