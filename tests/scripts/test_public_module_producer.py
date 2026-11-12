"""Resolve a shared module producer's exact dependency through real find_package."""
import subprocess
import tempfile
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PublicModuleProducerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.producer = self.root / "producer"
        (self.producer / "p").mkdir(parents=True)
        (self.producer / "i").mkdir()
        self.cache = self.producer / "p/CMakeCache.txt"
        self.consumer = self.root / "consumer"
        self.consumer.mkdir()
        (self.consumer / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.31)
project(producer_dependency LANGUAGES NONE)
find_package(fmt 1.0.0 EXACT CONFIG REQUIRED)
file(WRITE "${CMAKE_BINARY_DIR}/selected.txt" "${FMT_SELECTED}")
''')
        self.driver = self.root / "check.cmake"
        self.driver.write_text('''include("${ROOT}/tests/cmake/scripts/PublicModuleProducer.cmake")
gentest_public_module_dependency_cache_args(_dependency_args)
execute_process(
  COMMAND "${CMAKE_COMMAND}" -G Ninja -S "${CONSUMER}" -B "${BUILD}"
    ${_dependency_args} "-DCMAKE_PREFIX_PATH=${PREFIX}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "Dependency consumer failed: ${out}\n${err}")
endif()
''')

    def write_fmt(self, directory, version, selected):
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "fmt-config.cmake").write_text(f'set(FMT_SELECTED "{selected}")\n')
        (directory / "fmt-config-version.cmake").write_text(
            f'set(PACKAGE_VERSION "{version}")\n' + '''if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)
  set(PACKAGE_VERSION_COMPATIBLE TRUE)
  set(PACKAGE_VERSION_EXACT TRUE)
endif()
''')

    def check_consumer(self, prefix):
        build = self.root / "build"
        result = subprocess.run([
            "cmake", f"-DROOT={ROOT.as_posix()}", f"-DPUBLIC_MODULE_PRODUCER_ROOT={self.producer.as_posix()}",
            f"-DCONSUMER={self.consumer.as_posix()}", f"-DBUILD={build.as_posix()}",
            f"-DPREFIX={prefix.as_posix()}", "-P", str(self.driver)], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((build / "selected.txt").read_text(), "producer")

    def test_external_dependency_wins_over_incompatible_ambient_package(self):
        dependency = self.root / "producer dependency with spaces"
        self.write_fmt(dependency, "1.0.0", "producer")
        ambient = self.root / "ambient"
        self.write_fmt(ambient, "2.0.0", "ambient")
        self.cache.write_text(f"fmt_DIR:PATH={dependency.as_posix()}\n")
        self.check_consumer(ambient)

    def test_dependency_installed_with_producer_needs_no_external_cache_entry(self):
        self.cache.write_text("GENTEST_FMT_VERSION:INTERNAL=1.0.0\n")
        self.write_fmt(self.producer / "i/lib/cmake/fmt", "1.0.0", "producer")
        self.check_consumer(self.producer / "i")

    def test_not_found_cache_entry_keeps_installed_dependency_discoverable(self):
        self.cache.write_text("fmt_DIR:PATH=fmt_DIR-NOTFOUND\n")
        self.write_fmt(self.producer / "i/lib/cmake/fmt", "1.0.0", "producer")
        self.check_consumer(self.producer / "i")


if __name__ == "__main__":
    unittest.main()
