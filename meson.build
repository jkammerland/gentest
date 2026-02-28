project('gentest', 'cpp', default_options: ['cpp_std=c++20'])

fmt_dep = dependency('fmt', required: false)
cpp = meson.get_compiler('cpp')
fs = import('fs')

inc = include_directories('include', 'tests', 'third_party/include')

# Build the runtime as a static library (compiled, not header-only)
runtime_cargs = ['-DFMT_HEADER_ONLY']
runtime_cargs += cpp.get_supported_arguments(['-Wno-attributes'])
if fmt_dep.found()
  runtime_deps = [fmt_dep]
else
  runtime_deps = []
endif

libgentest_runtime = static_library(
  'gentest_runtime',
  [
    'src/bench_stats.cpp',
    'src/runner_case_invoker.cpp',
    'src/runner_cli.cpp',
    'src/runner_fixture_runtime.cpp',
    'src/runner_measured_executor.cpp',
    'src/runner_impl.cpp',
    'src/runner_reporting.cpp',
    'src/runner_selector.cpp',
    'src/runner_test_plan.cpp',
  ],
  include_directories: inc,
  cpp_args: runtime_cargs,
  dependencies: runtime_deps,
)

libgentest_main = static_library(
  'gentest_main',
  ['src/gentest_main.cpp'],
  include_directories: inc,
  cpp_args: runtime_cargs,
  dependencies: runtime_deps,
)

# Helper to attach code generation via an external generator binary.
# Configure the generator path via the Meson option `-Dcodegen_path=...`.
# Example:
#   meson setup build/meson -Dcodegen_path=build/debug-system/tools/gentest_codegen
#   meson compile -C build/meson
codegen_opt = get_option('codegen_path')
if codegen_opt == ''
  codegen_opt = join_paths(meson.project_source_root(), 'build', 'debug-system', 'tools', 'gentest_codegen')
elif not fs.is_absolute(codegen_opt)
  codegen_opt = join_paths(meson.project_source_root(), codegen_opt)
endif
codegen = codegen_opt

gen_args = ['--compdb', meson.current_build_dir()]
clang_args = [
  '-std=c++20',
  '-DGENTEST_CODEGEN=1',
  '-Wno-unknown-attributes',
  '-Wno-attributes',
  '-Wno-unknown-warning-option',
  '-I@SOURCE_ROOT@/include',
  '-I@SOURCE_ROOT@/tests',
]

tests_suites = ['unit', 'integration', 'fixtures', 'skiponly']

foreach s : tests_suites
  out_cpp = 'test_impl_@0@.cpp'.format(s)
  gen = custom_target(
    'gen_@0@'.format(s),
    output: out_cpp,
    input: 'tests/@0@/cases.cpp'.format(s),
    command: [codegen, '--output', '@OUTPUT@', gen_args, '@INPUT@', '--', clang_args],
    build_by_default: true
  )

  exe = executable(
    'gentest_@0@_meson'.format(s),
    [gen],
    include_directories: inc,
    link_with: [libgentest_main, libgentest_runtime],
    cpp_args: runtime_cargs,
    dependencies: runtime_deps
  )

  test('meson_@0@'.format(s), exe)
endforeach

# Convenience: run the CMake-based cross-compile POC from Meson.
bash = find_program('bash', required: false)
if bash.found()
  run_target(
    'poc_cross_aarch64_qemu',
    command: [bash, join_paths(meson.project_source_root(), 'scripts', 'poc_cross_aarch64_qemu.sh')]
  )
endif
