project('gentest', 'cpp', default_options: ['cpp_std=c++23'])

fmt_dep = dependency('fmt', required: false)
cpp = meson.get_compiler('cpp')

inc = include_directories('include', 'tests')

# Build the runtime as a static library (compiled, not header-only)
runtime_cargs = ['-DFMT_HEADER_ONLY']
if fmt_dep.found()
  runtime_deps = [fmt_dep]
else
  runtime_deps = []
endif

libgentest_runtime = static_library(
  'gentest_runtime',
  ['src/runner_impl.cpp'],
  include_directories: inc,
  cpp_args: runtime_cargs,
  dependencies: runtime_deps,
)

# Helper to attach code generation via an external generator binary.
# The path to gentest_codegen is taken from the environment variable
# GENTEST_CODEGEN. Example:
#   GENTEST_CODEGEN=build/debug-system/tools/gentest_codegen meson setup build/meson
#   meson compile -C build/meson
codegen = meson.get_env('GENTEST_CODEGEN')
if codegen == ''
  codegen = 'build/debug-system/tools/gentest_codegen'
endif

gen_args = ['--compdb', meson.current_build_dir(), '--']
clang_args = ['-std=c++23', '-I@SOURCE_ROOT@/include', '-I@SOURCE_ROOT@/tests']

tests_suites = ['unit', 'integration', 'fixtures', 'skiponly']

foreach s : tests_suites
  out_cpp = 'gen/@0@/test_impl.cpp'.format(s)
  gen = custom_target(
    'gen_@0@'.format(s),
    output: out_cpp,
    input: 'tests/@0@/cases.cpp'.format(s),
    command: [codegen, '--output', '@OUTPUT@', gen_args, '@INPUT@', clang_args],
    build_by_default: true
  )

  exe = executable(
    'gentest_@0@_meson'.format(s),
    ['tests/support/test_entry.cpp', gen],
    include_directories: inc,
    link_with: libgentest_runtime,
    cpp_args: runtime_cargs,
    dependencies: runtime_deps
  )

  test('meson_@0@'.format(s), exe)
endforeach

