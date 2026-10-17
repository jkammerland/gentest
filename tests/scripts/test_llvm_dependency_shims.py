"""Configure and link imported libraries using real archives and CMake mappings."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class ImportedConfigurationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temporary.name).resolve()
        cls.cmake = shutil.which("cmake")
        if not cls.cmake:
            raise RuntimeError("CMake is required for imported-library regression tests")
        producer = cls.root / "producer"
        producer.mkdir()
        (producer / "answer.c").write_text("int answer(void) { return 42; }\n")
        (producer / "custom.c").write_text("int answer(void) { return 73; }\n")
        (producer / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.25)
project(archives LANGUAGES C)
add_library(answer STATIC answer.c)
add_library(custom STATIC custom.c)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/paths-$<CONFIG>.txt"
     CONTENT "$<TARGET_FILE:answer>\n$<TARGET_FILE:custom>\n")
''')
        cls.run_command([cls.cmake, "-S", str(producer), "-B", str(producer / "build")])
        cls.run_command([cls.cmake, "--build", str(producer / "build"), "--config", "Release"])
        path_files = list((producer / "build").glob("paths-*.txt"))
        release = next((path for path in path_files if path.name == "paths-Release.txt"), path_files[0])
        cls.archive, cls.custom = release.read_text().splitlines()

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    @staticmethod
    def run_command(command):
        result = subprocess.run(command, text=True, capture_output=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        return result

    def configure(self, declarations, assertions="", *, configuration="MinSizeRel", expected=42, succeeds=True):
        source = self.root / self.id().split(".")[-1]
        source.mkdir()
        (source / "main.c").write_text(f"int answer(void);\nint main(void) {{ return answer() != {expected}; }}\n")
        (source / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.25)
project(imports LANGUAGES C)
include("{ROOT.as_posix()}/cmake/GentestLlvmDependencyShims.cmake")
# Match the pinned vcpkg toolchain's release mappings without requiring vcpkg.
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL "MinSizeRel;Release;None;")
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO "RelWithDebInfo;Release;None;")
{declarations}
gentest_normalize_llvm_imported_configs(${{LLVM_AVAILABLE_LIBS}} ${{CLANG_EXPORTED_TARGETS}})
{assertions}
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE component)
file(GENERATE OUTPUT "${{CMAKE_BINARY_DIR}}/exe-$<CONFIG>.txt" CONTENT "$<TARGET_FILE:consumer>")
''')
        result = subprocess.run([self.cmake, "-S", str(source), "-B", str(source / "build"),
                                 f"-DCMAKE_BUILD_TYPE={configuration}", f"-DCMAKE_CONFIGURATION_TYPES={configuration}"],
                                text=True, capture_output=True)
        self.assertEqual(result.returncode == 0, succeeds, result.stdout + result.stderr)
        if succeeds:
            self.run_command([self.cmake, "--build", str(source / "build"), "--config", configuration])
            paths = list((source / "build").glob("exe-*.txt"))
            path = next((p for p in paths if p.name == f"exe-{configuration}.txt"), paths[0])
            self.run_command([path.read_text()])
        return result

    def library(self, config="RELWITHDEBINFO", archive=None):
        suffix = f"_{config}" if config else ""
        return f'''add_library(component STATIC IMPORTED)
set(LLVM_AVAILABLE_LIBS component)
set_property(TARGET component PROPERTY IMPORTED_LOCATION{suffix} "{archive or self.archive}")
set_property(TARGET component PROPERTY IMPORTED_CONFIGURATIONS "{config}")
'''

    def test_new_exported_component_links_with_vcpkg_release_mapping(self):
        self.configure(self.library() + '''
set(CLANG_EXPORTED_TARGETS component new_component interface_tool executable_tool alias_component)
add_library(new_component STATIC IMPORTED)
set_property(TARGET new_component PROPERTY IMPORTED_LOCATION_RELWITHDEBINFO "''' + self.archive + '''")
add_library(interface_tool INTERFACE IMPORTED)
add_executable(executable_tool IMPORTED)
add_library(alias_component ALIAS component)
''', '''
get_target_property(actual new_component IMPORTED_LOCATION_MINSIZEREL)
if(NOT actual STREQUAL "''' + self.archive + '''")
  message(FATAL_ERROR "New package export did not receive configuration normalization")
endif()
''')

    def test_exact_configuration_has_priority(self):
        self.configure(self.library() + f'''
set_property(TARGET component PROPERTY IMPORTED_LOCATION_MINSIZEREL "{self.custom}")
''', expected=73)

    def test_configuration_independent_artifact(self):
        self.configure(self.library(config=""))

    def test_explicit_mapping_and_custom_artifact_are_preserved(self):
        self.configure(self.library() + f'''
set_property(TARGET component PROPERTY IMPORTED_LOCATION_CUSTOM "{self.custom}")
set_property(TARGET component PROPERTY MAP_IMPORTED_CONFIG_MINSIZEREL CUSTOM)
''', '''
get_target_property(mapping component MAP_IMPORTED_CONFIG_MINSIZEREL)
if(NOT mapping STREQUAL "CUSTOM")
  message(FATAL_ERROR "Explicit mapping changed")
endif()
''', expected=73)

    def test_copy_configuration_metadata_including_windows_artifact_pair(self):
        # Configure a DLL target without linking it on the host platform. Check
        # properties independently, including false and empty metadata values.
        declarations = self.library() + '''
add_library(dll SHARED IMPORTED)
set(CLANG_EXPORTED_TARGETS dll)
set_target_properties(dll PROPERTIES
  IMPORTED_LOCATION_RELWITHDEBINFO "release.dll"
  IMPORTED_IMPLIB_RELWITHDEBINFO "release.lib"
  IMPORTED_SONAME_RELWITHDEBINFO "release.so"
  IMPORTED_NO_SONAME_RELWITHDEBINFO FALSE
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELWITHDEBINFO "dependency_a;dependency_b"
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "CXX"
  IMPORTED_LINK_INTERFACE_LIBRARIES_RELWITHDEBINFO ""
  IMPORTED_LINK_INTERFACE_MULTIPLICITY_RELWITHDEBINFO 2)
'''
        assertions = '''
foreach(pair IN ITEMS "LOCATION|release.dll" "IMPLIB|release.lib" "SONAME|release.so"
    "NO_SONAME|FALSE" "LINK_DEPENDENT_LIBRARIES|dependency_a,dependency_b"
    "LINK_INTERFACE_LANGUAGES|CXX" "LINK_INTERFACE_LIBRARIES|" "LINK_INTERFACE_MULTIPLICITY|2")
  string(REPLACE "|" ";" fields "${pair}")
  list(GET fields 0 property)
  list(GET fields 1 expected)
  string(REPLACE "," ";" expected "${expected}")
  get_target_property(actual dll "IMPORTED_${property}_MINSIZEREL")
  if(NOT "${actual}" STREQUAL "${expected}")
    message(FATAL_ERROR "Lost ${property}: '${actual}', expected '${expected}'")
  endif()
endforeach()
'''
        self.configure(declarations, assertions)

    def test_debug_is_not_synthesized(self):
        result = self.configure(self.library() + '''
set_property(TARGET component PROPERTY MAP_IMPORTED_CONFIG_DEBUG Debug)
''', '''
get_property(has_debug TARGET component PROPERTY IMPORTED_LOCATION_DEBUG SET)
if(has_debug)
  message(FATAL_ERROR "Synthesized a Debug artifact")
endif()
''', configuration="Debug", succeeds=False)
        self.assertIn("IMPORTED_LOCATION", result.stderr)

    def test_debug_only_package_cannot_supply_release(self):
        result = self.configure(self.library(config="DEBUG"), succeeds=False)
        self.assertIn("IMPORTED_LOCATION", result.stderr)

    def test_missing_artifact_remains_a_configuration_failure(self):
        result = self.configure('add_library(component STATIC IMPORTED)\nset(LLVM_AVAILABLE_LIBS component)', succeeds=False)
        self.assertIn("IMPORTED_LOCATION", result.stderr)


if __name__ == "__main__":
    unittest.main()
