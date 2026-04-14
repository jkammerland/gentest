project('gentest', 'cpp', default_options: ['cpp_std=c++20'])

cpp = meson.get_compiler('cpp')
fs = import('fs')
py = import('python').find_installation('python3')

inc = include_directories('include', 'tests', 'third_party/include')
build_inc = include_directories('.')

# Resolve the repo-local codegen path early so non-CMake backends can also reuse
# the generator binary from a nearby host build.
codegen_opt = get_option('codegen_path')
if codegen_opt == ''
  if host_machine.system() == 'windows'
    codegen_opt = join_paths(meson.project_source_root(), 'build', 'host-codegen', 'tools', 'gentest_codegen.exe')
  else
    codegen_opt = join_paths(meson.project_source_root(), 'build', 'host-codegen', 'tools', 'gentest_codegen')
  endif
elif not fs.is_absolute(codegen_opt)
  codegen_opt = join_paths(meson.project_source_root(), codegen_opt)
endif
codegen = codegen_opt

codegen_host_clang_opt = get_option('codegen_host_clang')
if codegen_host_clang_opt != '' and not fs.is_absolute(codegen_host_clang_opt)
  codegen_host_clang_opt = join_paths(meson.project_source_root(), codegen_host_clang_opt)
endif
codegen_host_clang = codegen_host_clang_opt

codegen_clang_scan_deps_opt = get_option('codegen_clang_scan_deps')
if codegen_clang_scan_deps_opt != '' and not fs.is_absolute(codegen_clang_scan_deps_opt)
  codegen_clang_scan_deps_opt = join_paths(meson.project_source_root(), codegen_clang_scan_deps_opt)
endif
codegen_clang_scan_deps = codegen_clang_scan_deps_opt

codegen_tool_args = []
if codegen_host_clang != ''
  codegen_tool_args += ['--host-clang', codegen_host_clang]
endif
if codegen_clang_scan_deps != ''
  codegen_tool_args += ['--clang-scan-deps', codegen_clang_scan_deps]
endif

gentest_codegen = codegen
gentest_codegen_tool_args = codegen_tool_args

fmt_dep = dependency('fmt', required: false, method: 'pkg-config')
pkg_config = find_program('pkg-config', 'pkgconf', required: false)
fmt_pkgconfig_include_dirs = []
if fmt_dep.found() and pkg_config.found()
  fmt_pkgconfig_cflags_cmd = run_command(pkg_config, '--cflags-only-I', 'fmt', check: true)
  foreach arg : fmt_pkgconfig_cflags_cmd.stdout().strip().split()
    if arg.startswith('-I')
      fmt_pkgconfig_include_dirs += [arg.substring(2)]
    endif
  endforeach
endif
fmt_fallback_include_cmd = run_command(
  py,
  '-c',
  '''import os, sys
source_root = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1] else ''
codegen = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2] else ''
parent = os.path.dirname(os.path.dirname(codegen)) if codegen else ''
candidates = []
if parent:
    candidates.append(os.path.join(parent, '_deps', 'fmt-src', 'include'))
for candidate in candidates:
    if os.path.isdir(candidate):
        if source_root and os.path.commonpath([source_root, candidate]) == source_root:
            print(os.path.relpath(candidate, source_root))
        else:
            print(candidate)
        break
''',
  meson.project_source_root(),
  codegen,
  check: true
)
fmt_fallback_include_dir = fmt_fallback_include_cmd.stdout().strip()
use_fmt_fallback = not fmt_dep.found() and fmt_fallback_include_dir != ''
project_includes = [inc]
if use_fmt_fallback
  project_includes += [include_directories(fmt_fallback_include_dir)]
endif

# Build the runtime as a static library (compiled, not header-only)
runtime_cargs = ['-DFMT_HEADER_ONLY']
runtime_cargs += cpp.get_supported_arguments(['-Wno-attributes'])
if fmt_dep.found()
  runtime_deps = [fmt_dep]
else
  runtime_deps = []
endif

gentest_public_include = include_directories('include')
gentest_public_include_path = join_paths(meson.current_source_dir(), 'include')
gentest_third_party_include = include_directories('third_party/include')
gentest_third_party_include_path = join_paths(meson.current_source_dir(), 'third_party', 'include')
gentest_wrapper_template = files('meson/tu_wrapper.cpp.in')
gentest_textual_mock_template = files('meson/textual_mock_defs.cpp.in')
gentest_anchor_template = files('meson/anchor.cpp.in')
gentest_textual_public_header_template = files('meson/textual_mock_public.hpp.in')

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
    'src/runner_reporting_allure.cpp',
    'src/runner_selector.cpp',
    'src/runner_test_executor.cpp',
    'src/runner_test_plan.cpp',
  ],
  include_directories: project_includes,
  cpp_args: runtime_cargs,
  dependencies: runtime_deps,
)

libgentest_main = static_library(
  'gentest_main',
  ['src/gentest_main.cpp'],
  include_directories: project_includes,
  cpp_args: runtime_cargs,
  dependencies: runtime_deps,
)

gentest_runtime_dep = declare_dependency(
  include_directories: gentest_public_include,
  compile_args: runtime_cargs,
  dependencies: runtime_deps,
  link_with: libgentest_runtime,
)
gentest_main_dep = declare_dependency(
  include_directories: gentest_public_include,
  compile_args: runtime_cargs,
  dependencies: runtime_deps,
  link_with: libgentest_main,
)
gentest_dep = declare_dependency(
  include_directories: gentest_public_include,
  compile_args: runtime_cargs,
  dependencies: runtime_deps,
  link_with: [libgentest_main, libgentest_runtime],
)
meson.override_dependency('gentest', gentest_dep)

# Helper to attach code generation via an external generator binary.
# Configure the generator path via the Meson option `-Dcodegen_path=...`.
# When the target compiler is not Clang, pass a separate repo-local host Clang
# via `-Dcodegen_host_clang=...`. Meson remains textual-only here today.
# Example:
#   meson setup build/meson -Dcodegen_path=build/host-codegen/tools/gentest_codegen \
#     -Dcodegen_host_clang=/opt/llvm/bin/clang++ \
#     -Dcodegen_clang_scan_deps=/opt/llvm/bin/clang-scan-deps
#   meson compile -C build/meson

clang_args = [
  '-std=c++20',
  '-DGENTEST_CODEGEN=1',
  '-DFMT_HEADER_ONLY',
  '-Wno-unknown-attributes',
  '-Wno-attributes',
  '-Wno-unknown-warning-option',
  '-I@0@'.format(join_paths(meson.project_source_root(), 'include')),
  '-I@0@'.format(join_paths(meson.project_source_root(), 'tests')),
  '-I@0@'.format(join_paths(meson.project_source_root(), 'third_party', 'include')),
]
foreach fmt_include_dir : fmt_pkgconfig_include_dirs
  clang_args += ['-I@0@'.format(fmt_include_dir)]
endforeach
if use_fmt_fallback
  clang_args += ['-I@0@'.format(fmt_fallback_include_dir)]
endif
if get_option('build_self_tests')
  tests_suites = ['unit', 'integration', 'fixtures', 'skiponly']
# The repo-local Meson integration keeps the explicit 2-step model:
# generate mocks first, then attach codegen to the consuming test surface.
# Only textual targets are wired here today. Meson named-module support is
# intentionally unsupported for now.
wrapper_template = files('meson/tu_wrapper.cpp.in')
textual_mock_template = files('meson/textual_mock_defs.cpp.in')
anchor_template = files('meson/anchor.cpp.in')
textual_public_header_template = files('meson/textual_mock_public.hpp.in')
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
  wrapper_cfg = configuration_data()
  wrapper_cfg.set('SOURCE_FILE', join_paths(meson.project_source_root(), 'tests', s, 'cases.cpp'))
  wrapper_cfg.set('HEADER_FILE', wrapper_h)
  wrapper_src = configure_file(
    input: wrapper_template,
    output: wrapper_cpp,
    configuration: wrapper_cfg
  )
  gen = custom_target(
    'gen_@0@'.format(s),
    output: [wrapper_h],
    input: [wrapper_src] + support_inputs,
    command: [
      codegen,
      '--source-root', meson.project_source_root(),
      '--tu-out-dir', out_dir,
      '--tu-header-output', join_paths(out_dir, wrapper_h),
      codegen_tool_args,
      '@INPUT0@',
      '--',
      clang_args,
    ],
    build_by_default: true
  )

  exe = executable(
    'gentest_@0@_meson'.format(s),
    [wrapper_src, gen[0]],
    include_directories: project_includes,
    link_with: [libgentest_main, libgentest_runtime],
    cpp_args: runtime_cargs,
    dependencies: runtime_deps
  )

  test('meson_@0@'.format(s), exe)
endforeach

consumer_mock_out_dir = meson.current_build_dir()
consumer_mock_defs_cpp = 'consumer_textual_mocks_defs.cpp'
consumer_mock_anchor_cpp = 'consumer_textual_mocks_anchor.cpp'
consumer_mock_codegen_h = 'tu_0000_consumer_textual_mocks_defs.gentest.h'
consumer_mock_registry_h = 'consumer_textual_mocks_mock_registry.hpp'
consumer_mock_impl_h = 'consumer_textual_mocks_mock_impl.hpp'
consumer_mock_domain_registry_h = 'consumer_textual_mocks_mock_registry__domain_0000_header.hpp'
consumer_mock_domain_impl_h = 'consumer_textual_mocks_mock_impl__domain_0000_header.hpp'
consumer_mock_public_h = 'gentest_consumer_mocks.hpp'
consumer_mock_cfg = configuration_data()
consumer_mock_cfg.set('DEFS_FILE', join_paths(meson.project_source_root(), 'tests', 'consumer', 'header_mock_defs.hpp'))
consumer_mock_cfg.set('HEADER_FILE', consumer_mock_codegen_h)
consumer_mock_wrapper = configure_file(
  input: textual_mock_template,
  output: consumer_mock_defs_cpp,
  configuration: consumer_mock_cfg
)
consumer_anchor_cfg = configuration_data()
consumer_anchor_cfg.set('ANCHOR_SYMBOL', 'consumer_textual_mocks_anchor')
consumer_mock_anchor = configure_file(
  input: anchor_template,
  output: consumer_mock_anchor_cpp,
  configuration: consumer_anchor_cfg
)
consumer_public_cfg = configuration_data()
consumer_public_cfg.set('DEFS_FILE', join_paths(meson.project_source_root(), 'tests', 'consumer', 'header_mock_defs.hpp'))
consumer_public_cfg.set('REGISTRY_HEADER', consumer_mock_registry_h)
consumer_public_cfg.set('IMPL_HEADER', consumer_mock_impl_h)
consumer_mock_public = configure_file(
  input: textual_public_header_template,
  output: consumer_mock_public_h,
  configuration: consumer_public_cfg
)

consumer_mocks = custom_target(
  'gen_consumer_textual_mocks',
  output: [
    consumer_mock_codegen_h,
    consumer_mock_registry_h,
    consumer_mock_impl_h,
    consumer_mock_domain_registry_h,
    consumer_mock_domain_impl_h,
  ],
  input: [
    consumer_mock_wrapper,
  ] + support_inputs,
  command: [
    codegen,
    '--source-root', meson.project_source_root(),
    '--tu-out-dir', consumer_mock_out_dir,
    '--tu-header-output', join_paths(consumer_mock_out_dir, consumer_mock_codegen_h),
    '--mock-registry', join_paths(consumer_mock_out_dir, consumer_mock_registry_h),
    '--mock-impl', join_paths(consumer_mock_out_dir, consumer_mock_impl_h),
    '--mock-domain-registry-output', join_paths(consumer_mock_out_dir, consumer_mock_domain_registry_h),
    '--mock-domain-impl-output', join_paths(consumer_mock_out_dir, consumer_mock_domain_impl_h),
    '--discover-mocks',
    codegen_tool_args,
    '@INPUT0@',
    '--',
    clang_args,
  ],
  build_by_default: true
)

consumer_mock_lib = static_library(
  'gentest_consumer_textual_mocks_meson',
  [consumer_mock_wrapper, consumer_mock_anchor],
  include_directories: project_includes + [build_inc],
  cpp_args: runtime_cargs,
  dependencies: runtime_deps,
  link_with: [libgentest_runtime],
)

consumer_wrapper_cpp = 'tu_0000_consumer_textual_cases.gentest.cpp'
consumer_wrapper_h = 'tu_0000_consumer_textual_cases.gentest.h'
consumer_wrapper_cfg = configuration_data()
consumer_wrapper_cfg.set('SOURCE_FILE', join_paths(meson.project_source_root(), 'tests', 'consumer', 'cases.cpp'))
consumer_wrapper_cfg.set('HEADER_FILE', consumer_wrapper_h)
consumer_wrapper_src = configure_file(
  input: wrapper_template,
  output: consumer_wrapper_cpp,
  configuration: consumer_wrapper_cfg
)
consumer_codegen = custom_target(
  'gen_consumer_textual_cases',
  output: [consumer_wrapper_h],
  input: [consumer_wrapper_src, consumer_mocks] + support_inputs,
  command: [
    codegen,
    '--source-root', meson.project_source_root(),
    '--tu-out-dir', meson.current_build_dir(),
    '--tu-header-output', join_paths(meson.current_build_dir(), consumer_wrapper_h),
    codegen_tool_args,
    '@INPUT0@',
    '--',
    '-I@0@'.format(meson.current_build_dir()),
    clang_args,
  ],
  build_by_default: true
)

consumer_exe = executable(
  'gentest_consumer_textual_meson',
  [consumer_wrapper_src, consumer_codegen[0], 'tests/consumer/main.cpp'],
  include_directories: project_includes + [build_inc],
  link_with: [libgentest_main, libgentest_runtime, consumer_mock_lib],
  cpp_args: runtime_cargs,
  dependencies: runtime_deps
)

test('meson_consumer_textual', consumer_exe)

# Convenience: run the CMake-based cross-compile POC from Meson.
bash = find_program('bash', required: false)
if bash.found()
  run_target(
    'poc_cross_aarch64_qemu',
    command: [bash, join_paths(meson.project_source_root(), 'scripts', 'poc_cross_aarch64_qemu.sh')]
  )
  run_target(
    'poc_cross_riscv64_qemu',
    command: [bash, join_paths(meson.project_source_root(), 'scripts', 'poc_cross_riscv64_qemu.sh')]
  )
endif
endif
