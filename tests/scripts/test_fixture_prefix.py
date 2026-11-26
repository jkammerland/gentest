"""Check package fixture registration, compatibility, and lookup."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
RUNTIME = 'MultiThreaded$<$<CONFIG:Debug>:Debug>'
ITERATORS = '-D_ITERATOR_DEBUG_LEVEL=0 -D_HAS_ITERATOR_DEBUGGING=0'


class FixturePrefixTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.package = self.root / 'package'
        (self.package / 'p').mkdir(parents=True)
        (self.package / 'i').mkdir()
        self.compiler = (self.root / 'clang++.exe').as_posix()
        self.cmake = shutil.which('cmake')
        self.assertTrue(self.cmake, 'CMake is required')

    def check_prefix(self, *, windows=False, compiler=None, build_type='RelWithDebInfo',
                     runtime=None, flags=None, modules=False, producer_modules=False,
                     producer_type='RelWithDebInfo', toolchain='', producer_toolchain='', present=True):
        values = {
            'CMAKE_CXX_COMPILER': self.compiler,
            'CMAKE_BUILD_TYPE': producer_type,
            'CMAKE_TOOLCHAIN_FILE': producer_toolchain,
            'CMAKE_MSVC_RUNTIME_LIBRARY': RUNTIME if windows else '',
            'CMAKE_CXX_FLAGS_INIT': ITERATORS if windows else '',
            'GENTEST_PUBLIC_MODULES_ENABLED': 'ON' if producer_modules else 'OFF',
        }
        cache = self.package / 'p/CMakeCache.txt'
        cache.write_text(''.join(f'{key}:STRING={value}\n' for key, value in values.items()))
        if not present:
            cache.unlink()
        args = [f'-DCMAKE_CXX_COMPILER={compiler or self.compiler}',
                f'-DCMAKE_BUILD_TYPE={build_type}',
                f'-DGENTEST_ENABLE_PUBLIC_MODULES={"ON" if modules else "OFF"}']
        if toolchain:
            args.append(f'-DCMAKE_TOOLCHAIN_FILE={toolchain}')
        if runtime is not None:
            args.append(f'-DCMAKE_MSVC_RUNTIME_LIBRARY={runtime}')
        if flags is not None:
            args.append(f'-DCMAKE_CXX_FLAGS_INIT={flags}')
        script = self.root / 'check.cmake'
        script.write_text(f'''cmake_minimum_required(VERSION 3.25)
set(CMAKE_HOST_WIN32 {"TRUE" if windows else "FALSE"})
include("{ROOT.as_posix()}/tests/cmake/scripts/FixturePrefixContext.cmake")
set(GENTEST_FIXTURE_PACKAGE_ROOT "{self.package.as_posix()}")
set(args)
''' + ''.join(f'list(APPEND args [[{arg}]])\n' for arg in args) + '''
set(before "${args}")
gentest_fixture_prefix_cache_args(selected args)
if(NOT before STREQUAL args)
  message(FATAL_ERROR "Prefix selection changed the caller's explicit arguments")
endif()
file(WRITE "${CMAKE_CURRENT_LIST_DIR}/selected.txt" "${selected}")
''')
        result = subprocess.run([self.cmake, '-P', str(script)], text=True, capture_output=True)
        if not present:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('unavailable', result.stdout + result.stderr)
            return []
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        text = (self.root / 'selected.txt').read_text()
        return text.split(';') if text else []

    def test_linux_compatible_prefix(self):
        self.assertEqual(self.check_prefix(), [f'-DGENTEST_FIXTURE_PREFIX={self.package.as_posix()}/i'])

    def test_windows_inherits_matching_native_llvm_abi_defaults(self):
        selected = self.check_prefix(windows=True)
        self.assertIn(f'-DGENTEST_FIXTURE_PREFIX={self.package.as_posix()}/i', selected)
        self.assertIn(f'-DCMAKE_MSVC_RUNTIME_LIBRARY={RUNTIME}', selected)
        self.assertIn(f'-DCMAKE_CXX_FLAGS_INIT={ITERATORS}', selected)

    def test_explicit_matching_windows_abi_is_kept(self):
        selected = self.check_prefix(windows=True, runtime=RUNTIME, flags=ITERATORS)
        self.assertEqual(selected, [f'-DGENTEST_FIXTURE_PREFIX={self.package.as_posix()}/i'])

    def test_explicit_different_crt_rejects_prefix(self):
        self.assertEqual(self.check_prefix(windows=True, runtime='MultiThreadedDLL'), [])

    def test_explicit_empty_crt_does_not_get_overridden(self):
        self.assertEqual(self.check_prefix(windows=True, runtime=''), [])

    def test_explicit_different_iterator_abi_rejects_prefix(self):
        self.assertEqual(self.check_prefix(windows=True, flags='-D_ITERATOR_DEBUG_LEVEL=2'), [])

    def test_different_compiler_rejects_prefix(self):
        self.assertEqual(self.check_prefix(compiler='/another/compiler'), [])

    def test_different_configuration_rejects_prefix(self):
        self.assertEqual(self.check_prefix(build_type='Debug'), [])

    def test_different_module_mode_rejects_prefix(self):
        self.assertEqual(self.check_prefix(modules=True), [])

    def test_target_toolchain_must_match(self):
        toolchain = (self.root / 'target.cmake').as_posix()
        selected = self.check_prefix(toolchain=toolchain, producer_toolchain=toolchain)
        self.assertEqual(selected, [f'-DGENTEST_FIXTURE_PREFIX={self.package.as_posix()}/i'])
        other = (self.root / 'other-target.cmake').as_posix()
        self.assertEqual(self.check_prefix(toolchain=toolchain, producer_toolchain=other), [])

    def test_missing_prefix_is_an_error(self):
        self.check_prefix(present=False)

    def test_windows_registers_a_native_clang_package(self):
        script = self.root / 'producer.cmake'
        script.write_text(f'''cmake_minimum_required(VERSION 3.25)
set(WIN32 TRUE)
set(UNIX FALSE)
set(CMAKE_HOST_WIN32 TRUE)
set(PROJECT_NAME gentest)
set(CMAKE_C_COMPILER cl.exe)
set(CMAKE_CXX_COMPILER cl.exe)
set(CMAKE_CROSSCOMPILING FALSE)
include("{ROOT.as_posix()}/tests/cmake/FixturePrefix.cmake")
function(gentest_resolve_clang_fixture_compilers out_c out_cxx)
  set(${{out_c}} [[{self.root.as_posix()}/clang.exe]] PARENT_SCOPE)
  set(${{out_cxx}} [[{self.compiler}]] PARENT_SCOPE)
endfunction()
function(gentest_find_clang_scan_deps out_var compiler)
  set(${{out_var}} [[{self.root.as_posix()}/clang-scan-deps.exe]] PARENT_SCOPE)
endfunction()
gentest_fixture_prefix_package_args(args MODULES)
file(WRITE "${{CMAKE_CURRENT_LIST_DIR}}/producer.txt" "${{args}}")
''')
        result = subprocess.run([self.cmake, '-P', str(script)], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        selected = (self.root / 'producer.txt').read_text().split(';')
        self.assertIn('-DBUILD_CONFIG=RelWithDebInfo', selected)
        self.assertIn(f'-DPACKAGE_TEST_CXX_COMPILER={self.compiler}', selected)
        self.assertIn('-DPACKAGE_TEST_USE_MODULES=ON', selected)

    def test_cross_build_registers_only_opted_in_producer(self):
        script = self.root / 'producer.cmake'
        script.write_text(f'''cmake_minimum_required(VERSION 3.25)
set(UNIX TRUE)
set(WIN32 FALSE)
set(CMAKE_CROSSCOMPILING TRUE)
set(CMAKE_TOOLCHAIN_FILE "{self.root.as_posix()}/target.cmake")
set(PROJECT_NAME gentest)
include("{ROOT.as_posix()}/tests/cmake/FixturePrefix.cmake")
function(gentest_resolve_clang_fixture_compilers out_c out_cxx)
  set(${{out_c}} [[{self.root.as_posix()}/clang]] PARENT_SCOPE)
  set(${{out_cxx}} [[{self.compiler}]] PARENT_SCOPE)
endfunction()
function(gentest_find_clang_scan_deps out_var compiler)
  set(${{out_var}} [[{self.root.as_posix()}/clang-scan-deps]] PARENT_SCOPE)
endfunction()
gentest_fixture_prefix_package_args(default_args MODULES)
gentest_fixture_prefix_package_args(target_args TARGET_MODULES)
if(default_args)
  message(FATAL_ERROR "Unexpected package for a fixture that did not opt in")
endif()
file(WRITE "${{CMAKE_CURRENT_LIST_DIR}}/producer.txt" "${{target_args}}")
''')
        result = subprocess.run([self.cmake, '-P', str(script)], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        selected = (self.root / 'producer.txt').read_text().split(';')
        self.assertIn(f'-DPACKAGE_TEST_CXX_COMPILER={self.compiler}', selected)
        self.assertIn('-DPACKAGE_TEST_USE_MODULES=ON', selected)
        self.assertIn('-DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON', selected)

    def test_target_prefix_is_a_root_for_package_dependencies(self):
        prefix = self.package / 'i'
        for name in ('gentest', 'fmt'):
            directory = prefix / 'share' / name
            directory.mkdir(parents=True)
            config = f'add_library({name}::{name} INTERFACE IMPORTED)\n'
            if name == 'gentest':
                config = 'include(CMakeFindDependencyMacro)\nfind_dependency(fmt CONFIG REQUIRED)\n' + config
            (directory / f'{name}Config.cmake').write_text(config)

        sysroot = self.root / 'sysroot'
        sysroot.mkdir()
        toolchain = self.root / 'target.cmake'
        toolchain.write_text(f'''set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_FIND_ROOT_PATH "{sysroot.as_posix()}")
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
''')
        (self.root / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.25)
project(consumer NONE)
set(GENTEST_FIXTURE_PREFIX "{prefix.as_posix()}")
include("{ROOT.as_posix()}/tests/cmake/GentestFixtureDependency.cmake")
gentest_fixture_add_gentest(gentest)
if(NOT TARGET gentest::gentest OR NOT TARGET fmt::fmt)
  message(FATAL_ERROR "Target package or its dependency is missing")
endif()
''')
        result = subprocess.run([self.cmake, '-S', str(self.root), '-B', str(self.root / 'out'),
                                 f'-DCMAKE_TOOLCHAIN_FILE={toolchain}'], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
