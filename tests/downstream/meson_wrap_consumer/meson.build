project('gentest_downstream_wrap_consumer', 'cpp', default_options: ['cpp_std=c++20'])

fs = import('fs')

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

gentest_textual_mocks = [
  {
    'name': 'gentest_downstream_textual_mocks',
    'id': 'downstream_textual_mocks',
    'defs': 'tests/header_mock_defs.hpp',
    'public_header': 'gentest_downstream_mocks.hpp',
    'source_includes': ['tests'],
  },
]

gentest_textual_suites = [
  {
    'name': 'gentest_downstream_textual',
    'id': 'downstream_textual',
    'source': 'tests/cases.cpp',
    'main': files('tests/main.cpp'),
    'source_includes': ['tests'],
    'mocks': ['gentest_downstream_textual_mocks'],
    'test_name': 'meson_downstream_textual',
  },
]

subdir('subprojects/gentest/meson/textual')
