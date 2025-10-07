project('gentest', 'cpp', version: '1.0.0', default_options: ['cpp_std=c++23', 'warning_level=2'])

cpp = meson.get_compiler('cpp')

# Header-only interface dependency for consumers
inc_root = include_directories('include')

# Suppress attribute warnings on different compilers
gentest_cargs = []
if cpp.get_id() == 'clang'
  gentest_cargs += ['-Wno-unknown-attributes', '-Wno-attributes']
elif cpp.get_id() == 'gcc'
  gentest_cargs += ['-Wno-attributes']
elif cpp.get_id() == 'msvc'
  gentest_cargs += ['/wd5030']
endif

gentest_iface = declare_dependency(
  include_directories: inc_root,
  compile_args: gentest_cargs,
)

# External deps
fmt_dep = dependency('fmt', required: true, method: 'pkg-config', version: '>=10')

# Prefer monolithic libclang-cpp (covers clangTooling/AST/etc.)
clang_cpp = cpp.find_library('clang-cpp', required: true)

# Use llvm-config to get include dir and any system libs
llvm_config = find_program('llvm-config', required: true)
llvm_includedir = run_command(llvm_config, '--includedir').stdout().strip()
llvm_inc = include_directories(llvm_includedir)

thread_dep = dependency('threads')

# Build the code generator
gentest_codegen = executable(
  'gentest_codegen',
  files(
    'tools/src/main.cpp',
    'tools/src/parse_core.cpp',
    'tools/src/parse.cpp',
    'tools/src/discovery.cpp',
    'tools/src/mock_discovery.cpp',
    'tools/src/validate.cpp',
    'tools/src/emit.cpp',
    'tools/src/render_mocks.cpp',
    'tools/src/type_kind.cpp',
    'tools/src/render.cpp',
    'tools/src/tooling_support.cpp',
  ),
  include_directories: [inc_root, llvm_inc],
  dependencies: [fmt_dep, thread_dep],
  link_with: [clang_cpp],
  cpp_args: ['-DFMT_HEADER_ONLY', '"-DGENTEST_VERSION_STR=' + meson.project_version() + '"'],
  install: false,
)

# Test suites (only pass-only suites to keep CI green)
suites = [
  'unit',
  'integration',
  'skiponly',
  'fixtures',
  'templates',
  'mocking',
]

tests_dir = meson.current_source_dir() / 'tests'

gen = generator(
  gentest_codegen,
  output: [
    '@BASENAME@_test_impl.cpp',
    '@BASENAME@_mock_registry.hpp',
    '@BASENAME@_mock_impl.hpp',
  ],
  arguments: [
    '--output', '@OUTPUT0@',
    '--entry', 'gentest::run_all_tests',
    '--mock-registry', '@OUTPUT1@',
    '--mock-impl', '@OUTPUT2@',
    '@INPUT@',
    '--',
    '-std=c++23',
    '-Wno-unknown-attributes',
    '-Wno-attributes',
    '-I' + meson.current_source_dir() / 'include',
    '-I' + tests_dir,
  ],
)

foreach s : suites
  src = files(tests_dir / s / 'cases.cpp')
  outs = gen.process(src)
  impl = outs[0]
  reg  = '@0@_mock_registry.hpp'.format(s)
  implhdr = '@0@_mock_impl.hpp'.format(s)

  exe = executable(
    'gentest_@0@_tests'.format(s),
    [ 'tests/support/test_entry.cpp', impl ],
    include_directories: [inc_root, include_directories(tests_dir), include_directories(meson.current_build_dir())],
    dependencies: [fmt_dep, gentest_iface, thread_dep],
    cpp_args: [
      '-DFMT_HEADER_ONLY',
      '-DGENTEST_MOCK_REGISTRY_PATH="' + reg + '"',
      '-DGENTEST_MOCK_IMPL_PATH="' + implhdr + '"',
    ] + gentest_cargs,
    install: false,
  )
  test(s, exe)
endforeach

