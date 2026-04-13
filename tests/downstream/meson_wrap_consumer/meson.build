project('gentest_downstream_wrap_consumer', 'cpp', default_options: ['cpp_std=c++20'])

cpp = meson.get_compiler('cpp')
fs = import('fs')
py = import('python').find_installation('python3')

gentest_codegen_path = get_option('gentest_codegen_path')
gentest_codegen_host_clang = get_option('gentest_codegen_host_clang')
gentest_codegen_clang_scan_deps = get_option('gentest_codegen_clang_scan_deps')
if gentest_codegen_path == '' or gentest_codegen_host_clang == ''
  error('gentest downstream Meson wrap consumer requires gentest_codegen_path and gentest_codegen_host_clang')
endif
if not fs.is_absolute(gentest_codegen_path)
  gentest_codegen_path = join_paths(meson.project_source_root(), gentest_codegen_path)
endif
if not fs.is_absolute(gentest_codegen_host_clang)
  gentest_codegen_host_clang = join_paths(meson.project_source_root(), gentest_codegen_host_clang)
endif
if gentest_codegen_clang_scan_deps != '' and not fs.is_absolute(gentest_codegen_clang_scan_deps)
  gentest_codegen_clang_scan_deps = join_paths(meson.project_source_root(), gentest_codegen_clang_scan_deps)
endif

gentest_sp = subproject(
  'gentest',
  default_options: [
    'build_self_tests=false',
    'codegen_path=' + gentest_codegen_path,
    'codegen_host_clang=' + gentest_codegen_host_clang,
  ] + (gentest_codegen_clang_scan_deps != '' ? ['codegen_clang_scan_deps=' + gentest_codegen_clang_scan_deps] : []),
)
gentest_codegen = gentest_sp.get_variable('gentest_codegen')
gentest_codegen_tool_args = gentest_sp.get_variable('gentest_codegen_tool_args')
gentest_main_dep = gentest_sp.get_variable('gentest_main_dep')
gentest_runtime_dep = gentest_sp.get_variable('gentest_runtime_dep')
gentest_public_include = gentest_sp.get_variable('gentest_public_include')
gentest_public_include_path = gentest_sp.get_variable('gentest_public_include_path')
gentest_third_party_include = gentest_sp.get_variable('gentest_third_party_include')
gentest_third_party_include_path = gentest_sp.get_variable('gentest_third_party_include_path')
gentest_wrapper_template = gentest_sp.get_variable('gentest_wrapper_template')
gentest_textual_mock_template = gentest_sp.get_variable('gentest_textual_mock_template')
gentest_anchor_template = gentest_sp.get_variable('gentest_anchor_template')
gentest_textual_public_header_template = gentest_sp.get_variable('gentest_textual_public_header_template')

fixture_inc = include_directories('tests')
project_includes = [fixture_inc]

clang_args = [
  '-std=c++20',
  '-DGENTEST_CODEGEN=1',
  '-DFMT_HEADER_ONLY',
  '-Wno-unknown-attributes',
  '-Wno-attributes',
  '-Wno-unknown-warning-option',
  '-I@0@'.format(join_paths(meson.project_source_root(), 'tests')),
  '-I@0@'.format(gentest_public_include_path),
  '-I@0@'.format(gentest_third_party_include_path),
]

support_inputs_cmd = run_command(
  py,
  '-c',
  '''import glob
import os
import sys

fixture_tests = sys.argv[1]
fixture_tests = os.path.join(fixture_tests, 'tests')

patterns = [
  os.path.join(fixture_tests, '**', '*.h'),
  os.path.join(fixture_tests, '**', '*.hh'),
  os.path.join(fixture_tests, '**', '*.hpp'),
  os.path.join(fixture_tests, '**', '*.hxx'),
  os.path.join(fixture_tests, '**', '*.inc'),
]
matches = sorted({path for pattern in patterns for path in glob.glob(pattern, recursive=True)})
print("\n".join(matches))
''',
  meson.project_source_root(),
  check: true
)
support_inputs = []
if support_inputs_cmd.stdout().strip() != ''
  support_inputs = support_inputs_cmd.stdout().strip().split('\n')
endif

consumer_mock_out_dir = meson.current_build_dir()
consumer_mock_defs_cpp = 'downstream_textual_mocks_defs.cpp'
consumer_mock_anchor_cpp = 'downstream_textual_mocks_anchor.cpp'
consumer_mock_codegen_h = 'tu_0000_downstream_textual_mocks_defs.gentest.h'
consumer_mock_registry_h = 'downstream_textual_mocks_mock_registry.hpp'
consumer_mock_impl_h = 'downstream_textual_mocks_mock_impl.hpp'
consumer_mock_domain_registry_h = 'downstream_textual_mocks_mock_registry__domain_0000_header.hpp'
consumer_mock_domain_impl_h = 'downstream_textual_mocks_mock_impl__domain_0000_header.hpp'
consumer_mock_public_h = 'gentest_downstream_mocks.hpp'

consumer_mock_cfg = configuration_data()
consumer_mock_cfg.set('DEFS_FILE', join_paths(meson.project_source_root(), 'tests', 'header_mock_defs.hpp'))
consumer_mock_cfg.set('HEADER_FILE', consumer_mock_codegen_h)
consumer_mock_wrapper = configure_file(
  input: gentest_textual_mock_template,
  output: consumer_mock_defs_cpp,
  configuration: consumer_mock_cfg
)
consumer_anchor_cfg = configuration_data()
consumer_anchor_cfg.set('ANCHOR_SYMBOL', 'downstream_textual_mocks_anchor')
consumer_mock_anchor = configure_file(
  input: gentest_anchor_template,
  output: consumer_mock_anchor_cpp,
  configuration: consumer_anchor_cfg
)
consumer_public_cfg = configuration_data()
consumer_public_cfg.set('DEFS_FILE', join_paths(meson.project_source_root(), 'tests', 'header_mock_defs.hpp'))
consumer_public_cfg.set('REGISTRY_HEADER', consumer_mock_registry_h)
consumer_public_cfg.set('IMPL_HEADER', consumer_mock_impl_h)
consumer_mock_public = configure_file(
  input: gentest_textual_public_header_template,
  output: consumer_mock_public_h,
  configuration: consumer_public_cfg
)

consumer_mocks = custom_target(
  'gen_downstream_textual_mocks',
  output: [
    consumer_mock_codegen_h,
    consumer_mock_registry_h,
    consumer_mock_impl_h,
    consumer_mock_domain_registry_h,
    consumer_mock_domain_impl_h,
  ],
  input: [consumer_mock_wrapper] + support_inputs,
  command: [
    gentest_codegen,
    '--source-root', meson.project_source_root(),
    '--tu-out-dir', consumer_mock_out_dir,
    '--tu-header-output', join_paths(consumer_mock_out_dir, consumer_mock_codegen_h),
    '--mock-registry', join_paths(consumer_mock_out_dir, consumer_mock_registry_h),
    '--mock-impl', join_paths(consumer_mock_out_dir, consumer_mock_impl_h),
    '--mock-domain-registry-output', join_paths(consumer_mock_out_dir, consumer_mock_domain_registry_h),
    '--mock-domain-impl-output', join_paths(consumer_mock_out_dir, consumer_mock_domain_impl_h),
    '--discover-mocks',
    gentest_codegen_tool_args,
    '@INPUT0@',
    '--',
    clang_args,
  ],
  build_by_default: true
)

consumer_mock_lib = static_library(
  'gentest_downstream_textual_mocks',
  [consumer_mock_wrapper, consumer_mock_anchor],
  include_directories: project_includes + [include_directories('.')],
  cpp_args: ['-DFMT_HEADER_ONLY', '-Wno-attributes'],
  dependencies: [gentest_runtime_dep],
)

consumer_wrapper_cpp = 'tu_0000_downstream_textual_cases.gentest.cpp'
consumer_wrapper_h = 'tu_0000_downstream_textual_cases.gentest.h'
consumer_wrapper_cfg = configuration_data()
consumer_wrapper_cfg.set('SOURCE_FILE', join_paths(meson.project_source_root(), 'tests', 'cases.cpp'))
consumer_wrapper_cfg.set('HEADER_FILE', consumer_wrapper_h)
consumer_wrapper_src = configure_file(
  input: gentest_wrapper_template,
  output: consumer_wrapper_cpp,
  configuration: consumer_wrapper_cfg
)
consumer_codegen = custom_target(
  'gen_downstream_textual_cases',
  output: [consumer_wrapper_h],
  input: [consumer_wrapper_src, consumer_mocks] + support_inputs,
  command: [
    gentest_codegen,
    '--source-root', meson.project_source_root(),
    '--tu-out-dir', meson.current_build_dir(),
    '--tu-header-output', join_paths(meson.current_build_dir(), consumer_wrapper_h),
    gentest_codegen_tool_args,
    '@INPUT0@',
    '--',
    '-I@0@'.format(meson.current_build_dir()),
    clang_args,
  ],
  build_by_default: true
)

consumer_exe = executable(
  'gentest_downstream_textual',
  [consumer_wrapper_src, consumer_codegen[0], 'tests/main.cpp'],
  include_directories: project_includes + [include_directories('.')],
  link_with: [consumer_mock_lib],
  dependencies: [gentest_main_dep, gentest_runtime_dep],
)

test('meson_downstream_textual', consumer_exe)
