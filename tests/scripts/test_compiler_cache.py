"""Exercise the fixture compiler cache without changing source-location semantics."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class CompilerCacheTests(unittest.TestCase):
    def test_explicit_cache_mode_reaches_nested_tests(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            (root / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.31)
project(cache_environment LANGUAGES NONE)
include(CTest)
include("{ROOT.as_posix()}/cmake/GentestTestCompilerCache.cmake")
set(GENTEST_TEST_COMPILER_CACHE OFF)
add_test(NAME probe COMMAND "${{CMAKE_COMMAND}}" -E true)
gentest_configure_test_compiler_cache(probe)
''')
            # This property-only fixture uses one configuration even when the
            # host defaults to Visual Studio or another multi-config generator.
            env = dict(os.environ, GENTEST_TEST_COMPILER_CACHE='ON', CMAKE_GENERATOR='Ninja Multi-Config')
            result = subprocess.run(['cmake', '-G', 'Ninja', '-S', str(root), '-B', str(root / 'build')], env=env, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run(['ctest', '--test-dir', str(root / 'build'), '--show-only=json-v1'], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            tests = json.loads(result.stdout)['tests']
            self.assertEqual(len(tests), 1)
            properties = {p['name']: p['value'] for p in tests[0]['properties']}
            self.assertIn('GENTEST_TEST_COMPILER_CACHE=OFF', properties['ENVIRONMENT'])

    def test_unsupported_host_reports_the_complete_diagnostic(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            (root / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.31)
project(cache_unsupported LANGUAGES NONE)
set(CMAKE_HOST_UNIX FALSE)
include("{ROOT.as_posix()}/cmake/GentestTestCompilerCache.cmake")
gentest_enable_test_compiler_cache()
''')
            result = subprocess.run(['cmake', '-G', 'Ninja', '-S', str(root), '-B', str(root / 'build')], text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            diagnostics = ' '.join((result.stdout + result.stderr).split())
            self.assertIn('GENTEST_TEST_COMPILER_CACHE requires a GNU-style C++ compiler on a Unix host', diagnostics)

    def test_existing_custom_launcher_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            (root / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.31)
project(cache_guard LANGUAGES NONE)
set(CMAKE_HOST_UNIX TRUE)
set(CMAKE_CXX_COMPILER_ID Clang)
set(CMAKE_CXX_COMPILER_FRONTEND_VARIANT GNU)
set(GENTEST_CCACHE_EXECUTABLE "${{CMAKE_COMMAND}}")
set(CMAKE_CXX_COMPILER_LAUNCHER custom_launcher)
include("{ROOT.as_posix()}/cmake/GentestTestCompilerCache.cmake")
gentest_enable_test_compiler_cache()
''')
            result = subprocess.run(['cmake', '-S', str(root), '-B', str(root / 'build')], text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('cannot replace CMAKE_CXX_COMPILER_LAUNCHER', result.stdout + result.stderr)

    def test_compiler_cache_preserves_locations_and_header_invalidation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            source, build = root / 'source', root / 'build'
            source.mkdir()
            (source / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.31)
project(cache_contract LANGUAGES CXX)
file(WRITE "${{CMAKE_BINARY_DIR}}/compiler.txt" "${{CMAKE_CXX_COMPILER_ID}};${{CMAKE_CXX_COMPILER_FRONTEND_VARIANT}}")
include("{ROOT.as_posix()}/cmake/GentestTestCompilerCache.cmake")
gentest_enable_test_compiler_cache()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_executable(probe main.cpp)
''')
            (source / 'main.cpp').write_text('#include <cstdio>\n#include "value.hpp"\nint main() { std::puts(__FILE__); std::printf("%d\\n", value); }\n')
            header = source / 'value.hpp'
            header.write_text('inline constexpr int value = 42;\n')
            env = dict(os.environ, CCACHE_DIR=str(root / 'cache'), CCACHE_COMPILERCHECK='content')
            env.pop('CCACHE_DISABLE', None)
            command = ['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Debug']
            result = subprocess.run(command, env=env, text=True, capture_output=True)
            compiler = (build / 'compiler.txt').read_text().split(';')
            if os.name == 'nt' or compiler[0] not in ('GNU', 'Clang', 'AppleClang') or compiler[1] == 'MSVC':
                self.assertNotEqual(result.returncode, 0)
                diagnostics = ' '.join((result.stdout + result.stderr).split())
                self.assertIn('GNU-style C++ compiler on a Unix host', diagnostics)
                return
            if not shutil.which('ccache'):
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('GENTEST_CCACHE_EXECUTABLE', result.stdout + result.stderr)
                return
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            database = json.loads((build / 'compile_commands.json').read_text())
            self.assertEqual(len(database), 1)
            self.assertIn('-fdebug-prefix-map=', database[0]['command'])
            probe = build / 'probe'
            for value in (42, 99):
                if value == 99:
                    header.write_text('inline constexpr int value = 99;\n')
                result = subprocess.run(['cmake', '--build', str(build)], env=env, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(probe)], env=env, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                lines = result.stdout.splitlines()
                self.assertTrue(Path(lines[0]).samefile(source / 'main.cpp'), result.stdout)
                self.assertEqual(lines[1:], [str(value)])


if __name__ == '__main__':
    unittest.main()
