project('gentest', 'cpp', default_options: ['cpp_std=c++20'])

fmt_dep = dependency('fmt', required: false)
cpp = meson.get_compiler('cpp')
fs = import('fs')
py = import('python').find_installation('python3')

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
    'src/runtime_context.cpp',
    'src/runner_case_invoker.cpp',
    'src/runner_cli.cpp',
    'src/runner_fixture_runtime.cpp',
    'src/runner_measured_executor.cpp',
    'src/runner_measured_format.cpp',
    'src/runner_measured_report.cpp',
    'src/runner_impl.cpp',
    'src/runner_orchestrator.cpp',
    'src/runner_reporting.cpp',
    'src/runner_selector.cpp',
    'src/runner_test_executor.cpp',
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
  if host_machine.system() == 'windows'
    codegen_opt = join_paths(meson.project_source_root(), 'build', 'debug-system', 'tools', 'gentest_codegen.exe')
  else
    codegen_opt = join_paths(meson.project_source_root(), 'build', 'debug-system', 'tools', 'gentest_codegen')
  endif
elif not fs.is_absolute(codegen_opt)
  codegen_opt = join_paths(meson.project_source_root(), codegen_opt)
endif
codegen = codegen_opt

clang_args = [
  '-std=c++20',
  '-DGENTEST_CODEGEN=1',
  '-DFMT_HEADER_ONLY',
  '-Wno-unknown-attributes',
  '-Wno-attributes',
  '-Wno-unknown-warning-option',
  '-I@SOURCE_ROOT@/include',
  '-I@SOURCE_ROOT@/tests',
  '-I@SOURCE_ROOT@/third_party/include',
]
clang_arg_flags = []
foreach arg : clang_args
  clang_arg_flags += ['--clang-arg=@0@'.format(arg)]
endforeach

tests_suites = ['unit', 'integration', 'fixtures', 'skiponly']
buildsystem_codegen = files('scripts/gentest_buildsystem_codegen.py')
support_inputs_cmd = run_command(
  py,
  '-c',
  '''import glob
patterns = [
  "include/**/*.h", "include/**/*.hh", "include/**/*.hpp", "include/**/*.hxx", "include/**/*.inc",
  "third_party/include/**/*.h", "third_party/include/**/*.hh", "third_party/include/**/*.hpp",
  "third_party/include/**/*.hxx", "tests/**/*.h", "tests/**/*.hh", "tests/**/*.hpp",
  "tests/**/*.hxx", "tests/**/*.inc"
]
matches = sorted({path for pattern in patterns for path in glob.glob(pattern, recursive=True)})
print("\n".join(matches))
''',
  check: true
)
support_inputs = []
if support_inputs_cmd.stdout().strip() != ''
  support_inputs = support_inputs_cmd.stdout().strip().split('\n')
endif

foreach s : tests_suites
  out_dir = meson.current_build_dir()
  wrapper_cpp = 'tu_0000_@0@_cases.gentest.cpp'.format(s)
  wrapper_h = 'tu_0000_@0@_cases.gentest.h'.format(s)
  gen = custom_target(
    'gen_@0@'.format(s),
    output: [wrapper_cpp, wrapper_h],
    input: ['tests/@0@/cases.cpp'.format(s), buildsystem_codegen] + support_inputs,
    command: [
      py,
      buildsystem_codegen,
      '--codegen', codegen,
      '--source-root', meson.project_source_root(),
      '--out-dir', out_dir,
      '--wrapper-output', join_paths(out_dir, wrapper_cpp),
      '--header-output', join_paths(out_dir, wrapper_h),
      '--source-file', '@INPUT0@',
      '--compdb', meson.current_build_dir(),
      clang_arg_flags,
    ],
    build_by_default: true
  )

  exe = executable(
    'gentest_@0@_meson'.format(s),
    [gen[0]],
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
