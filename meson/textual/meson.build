if not is_variable('gentest_sp')
  error('gentest Meson textual helper requires gentest_sp = subproject(\'gentest\', ...) before subdir(\'subprojects/gentest/meson/textual\')')
endif

cpp = meson.get_compiler('cpp')
fs = import('fs')
py = import('python').find_installation('python3')

gentest_codegen = gentest_sp.get_variable('gentest_codegen')
gentest_codegen_tool_args = gentest_sp.get_variable('gentest_codegen_tool_args')
gentest_main_dep = gentest_sp.get_variable('gentest_main_dep')
gentest_runtime_dep = gentest_sp.get_variable('gentest_runtime_dep')
gentest_public_include_path = gentest_sp.get_variable('gentest_public_include_path')
gentest_third_party_include_path = gentest_sp.get_variable('gentest_third_party_include_path')
gentest_wrapper_template = gentest_sp.get_variable('gentest_wrapper_template')
gentest_textual_mock_template = gentest_sp.get_variable('gentest_textual_mock_template')
gentest_anchor_template = gentest_sp.get_variable('gentest_anchor_template')
gentest_textual_public_header_template = gentest_sp.get_variable('gentest_textual_public_header_template')
gentest_fmt_pkgconfig_include_dirs = gentest_sp.get_variable('fmt_pkgconfig_include_dirs')
gentest_fmt_fallback_include_dir = gentest_sp.get_variable('fmt_fallback_include_dir')

if not is_variable('gentest_textual_mocks')
  gentest_textual_mocks = []
endif
if not is_variable('gentest_textual_suites')
  gentest_textual_suites = []
endif

gentest_textual_base_clang_args = [
  '-std=c++20',
  '-DGENTEST_CODEGEN=1',
  '-DFMT_HEADER_ONLY',
  '-Wno-unknown-attributes',
  '-Wno-attributes',
  '-Wno-unknown-warning-option',
  '-I@0@'.format(meson.current_build_dir()),
  '-I@0@'.format(gentest_public_include_path),
  '-I@0@'.format(gentest_third_party_include_path),
]
foreach fmt_include_dir : gentest_fmt_pkgconfig_include_dirs
  gentest_textual_base_clang_args += ['-I@0@'.format(fmt_include_dir)]
endforeach
if gentest_fmt_fallback_include_dir != ''
  gentest_textual_base_clang_args += ['-I@0@'.format(gentest_fmt_fallback_include_dir)]
endif

gentest_textual_base_cpp_args = [
  '-DFMT_HEADER_ONLY',
  '-I@0@'.format(meson.current_build_dir()),
] + cpp.get_supported_arguments(['-Wno-attributes'])

gentest_textual_support_seed_paths = []
foreach mock_decl : gentest_textual_mocks
  if mock_decl.get('kind', 'textual') != 'textual'
    error('gentest Meson helper is textual-only; named modules are intentionally unsupported')
  endif
  gentest_textual_support_seed_paths += [mock_decl['defs']]
  gentest_textual_support_seed_paths += mock_decl.get('source_includes', [])
endforeach
foreach suite_decl : gentest_textual_suites
  if suite_decl.get('kind', 'textual') != 'textual'
    error('gentest Meson helper is textual-only; named modules are intentionally unsupported')
  endif
  gentest_textual_support_seed_paths += [suite_decl['source']]
  gentest_textual_support_seed_paths += suite_decl.get('source_includes', [])
endforeach

gentest_textual_support_inputs_cmd = run_command(
  py,
  '-c',
  '''import glob
import os
import sys

root = os.path.abspath(sys.argv[1])
seeds = sys.argv[2].split("\\n") if len(sys.argv) > 2 and sys.argv[2] else []
roots = set()
headers = set()
suffixes = ('.h', '.hh', '.hpp', '.hxx', '.inc')
for seed in seeds:
    path = seed if os.path.isabs(seed) else os.path.join(root, seed)
    path = os.path.normpath(path)
    if os.path.isfile(path):
        if path.endswith(suffixes):
            headers.add(path)
        roots.add(os.path.dirname(path))
    else:
        roots.add(path)
for directory in sorted(roots):
    if not os.path.isdir(directory):
        continue
    for suffix in suffixes:
        headers.update(glob.glob(os.path.join(directory, '**', '*' + suffix), recursive=True))
print("\\n".join(sorted(os.path.normpath(path) for path in headers)))
''',
  meson.project_source_root(),
  '\n'.join(gentest_textual_support_seed_paths),
  check: true
)
gentest_textual_support_inputs = []
if gentest_textual_support_inputs_cmd.stdout().strip() != ''
  gentest_textual_support_inputs = gentest_textual_support_inputs_cmd.stdout().strip().split('\n')
endif

gentest_textual_mock_targets = {}
gentest_textual_mock_codegen_targets = {}
gentest_textual_mock_public_headers = {}

foreach mock_decl : gentest_textual_mocks
  mock_name = mock_decl['name']
  mock_id = mock_decl.get('id', mock_name)
  mock_defs = mock_decl['defs']
  mock_defs_abs = mock_defs
  if not fs.is_absolute(mock_defs_abs)
    mock_defs_abs = join_paths(meson.project_source_root(), mock_defs_abs)
  endif

  mock_source_includes = mock_decl.get('source_includes', [])
  mock_clang_args = gentest_textual_base_clang_args + mock_decl.get('clang_args', [])
  mock_cpp_args = gentest_textual_base_cpp_args + mock_decl.get('cpp_args', [])
  mock_include_dirs = mock_decl.get('include_directories', [])
  foreach include_path : mock_source_includes
    include_abs = include_path
    if not fs.is_absolute(include_abs)
      include_abs = join_paths(meson.project_source_root(), include_abs)
    endif
    mock_clang_args += ['-I@0@'.format(include_abs)]
    mock_cpp_args += ['-I@0@'.format(include_abs)]
  endforeach

  mock_defs_cpp = '@0@_defs.cpp'.format(mock_id)
  mock_anchor_cpp = '@0@_anchor.cpp'.format(mock_id)
  mock_codegen_h = 'tu_0000_@0@_defs.gentest.h'.format(mock_id)
  mock_depfile = '@0@.d'.format(mock_codegen_h)
  mock_registry_h = '@0@_mock_registry.hpp'.format(mock_id)
  mock_impl_h = '@0@_mock_impl.hpp'.format(mock_id)
  mock_domain_registry_h = '@0@_mock_registry__domain_0000_header.hpp'.format(mock_id)
  mock_domain_impl_h = '@0@_mock_impl__domain_0000_header.hpp'.format(mock_id)
  mock_public_h = mock_decl.get('public_header', '@0@.hpp'.format(mock_name))

  mock_cfg = configuration_data()
  mock_cfg.set('DEFS_FILE', mock_defs_abs)
  mock_cfg.set('HEADER_FILE', mock_codegen_h)
  mock_wrapper = configure_file(
    input: gentest_textual_mock_template,
    output: mock_defs_cpp,
    configuration: mock_cfg
  )
  mock_anchor_cfg = configuration_data()
  mock_anchor_cfg.set('ANCHOR_SYMBOL', '@0@_anchor'.format(mock_id))
  mock_anchor = configure_file(
    input: gentest_anchor_template,
    output: mock_anchor_cpp,
    configuration: mock_anchor_cfg
  )
  mock_public_cfg = configuration_data()
  mock_public_cfg.set('DEFS_FILE', mock_defs_abs)
  mock_public_cfg.set('REGISTRY_HEADER', mock_registry_h)
  mock_public_cfg.set('IMPL_HEADER', mock_impl_h)
  mock_public = configure_file(
    input: gentest_textual_public_header_template,
    output: mock_public_h,
    configuration: mock_public_cfg
  )

  mock_codegen = custom_target(
    'gen_@0@'.format(mock_id),
    output: [
      mock_codegen_h,
      mock_registry_h,
      mock_impl_h,
      mock_domain_registry_h,
      mock_domain_impl_h,
    ],
    input: [mock_wrapper] + gentest_textual_support_inputs,
    command: [
      gentest_codegen,
      '--source-root', meson.project_source_root(),
      '--tu-out-dir', meson.current_build_dir(),
      '--tu-header-output', join_paths(meson.current_build_dir(), mock_codegen_h),
      '--mock-registry', join_paths(meson.current_build_dir(), mock_registry_h),
      '--mock-impl', join_paths(meson.current_build_dir(), mock_impl_h),
      '--mock-domain-registry-output', join_paths(meson.current_build_dir(), mock_domain_registry_h),
      '--mock-domain-impl-output', join_paths(meson.current_build_dir(), mock_domain_impl_h),
      '--discover-mocks',
      '--depfile', join_paths(meson.current_build_dir(), mock_depfile),
      gentest_codegen_tool_args,
      '@INPUT0@',
      '--',
      mock_clang_args,
    ],
    depfile: mock_depfile,
    build_by_default: mock_decl.get('build_by_default', true)
  )

  mock_lib = static_library(
    mock_name,
    [mock_wrapper, mock_anchor, mock_codegen[0]],
    include_directories: mock_include_dirs,
    cpp_args: mock_cpp_args,
    dependencies: [gentest_runtime_dep] + mock_decl.get('dependencies', []),
    install: mock_decl.get('install', false),
  )

  gentest_textual_mock_targets += {mock_name: mock_lib}
  gentest_textual_mock_codegen_targets += {mock_name: mock_codegen}
  gentest_textual_mock_public_headers += {mock_name: mock_public}
endforeach

gentest_textual_suite_targets = {}
gentest_textual_suite_codegen_targets = {}

foreach suite_decl : gentest_textual_suites
  suite_name = suite_decl['name']
  suite_id = suite_decl.get('id', suite_name)
  suite_source = suite_decl['source']
  suite_source_abs = suite_source
  if not fs.is_absolute(suite_source_abs)
    suite_source_abs = join_paths(meson.project_source_root(), suite_source_abs)
  endif
  suite_main = suite_decl.get('main', [])

  suite_source_includes = suite_decl.get('source_includes', [])
  suite_clang_args = gentest_textual_base_clang_args + suite_decl.get('clang_args', [])
  suite_cpp_args = gentest_textual_base_cpp_args + suite_decl.get('cpp_args', [])
  suite_include_dirs = suite_decl.get('include_directories', [])
  foreach include_path : suite_source_includes
    include_abs = include_path
    if not fs.is_absolute(include_abs)
      include_abs = join_paths(meson.project_source_root(), include_abs)
    endif
    suite_clang_args += ['-I@0@'.format(include_abs)]
    suite_cpp_args += ['-I@0@'.format(include_abs)]
  endforeach

  suite_mock_codegen_inputs = []
  suite_mock_libraries = []
  foreach suite_mock_name : suite_decl.get('mocks', [])
    suite_mock_codegen_inputs += [gentest_textual_mock_codegen_targets[suite_mock_name]]
    suite_mock_libraries += [gentest_textual_mock_targets[suite_mock_name]]
  endforeach

  suite_wrapper_cpp = 'tu_0000_@0@_cases.gentest.cpp'.format(suite_id)
  suite_wrapper_h = 'tu_0000_@0@_cases.gentest.h'.format(suite_id)
  suite_depfile = '@0@.d'.format(suite_wrapper_h)
  suite_manifest = '@0@.artifact_manifest.json'.format(suite_name)

  suite_wrapper_cfg = configuration_data()
  suite_wrapper_cfg.set('SOURCE_FILE', suite_source_abs)
  suite_wrapper_cfg.set('HEADER_FILE', suite_wrapper_h)
  suite_wrapper_src = configure_file(
    input: gentest_wrapper_template,
    output: suite_wrapper_cpp,
    configuration: suite_wrapper_cfg
  )
  suite_codegen = custom_target(
    'gen_@0@'.format(suite_id),
    output: [suite_wrapper_h, suite_manifest],
    input: [suite_wrapper_src] + suite_mock_codegen_inputs + gentest_textual_support_inputs,
    command: [
      gentest_codegen,
      '--source-root', meson.project_source_root(),
      '--tu-out-dir', meson.current_build_dir(),
      '--tu-header-output', join_paths(meson.current_build_dir(), suite_wrapper_h),
      '--depfile', join_paths(meson.current_build_dir(), suite_depfile),
      '--artifact-manifest', join_paths(meson.current_build_dir(), suite_manifest),
      '--artifact-owner-source', suite_source_abs,
      '--compile-context-id', '@0@:@1@'.format(suite_name, suite_source_abs),
      gentest_codegen_tool_args,
      '@INPUT0@',
      '--',
      suite_clang_args,
    ],
    depfile: suite_depfile,
    build_by_default: suite_decl.get('build_by_default', true)
  )

  suite_sources = [suite_wrapper_src, suite_codegen[0]]
  suite_sources += suite_main
  suite_target = executable(
    suite_name,
    suite_sources,
    include_directories: suite_include_dirs,
    link_with: suite_mock_libraries + suite_decl.get('link_with', []),
    dependencies: [gentest_main_dep, gentest_runtime_dep] + suite_decl.get('dependencies', []),
    cpp_args: suite_cpp_args,
    install: suite_decl.get('install', false),
  )
  if suite_decl.get('test', true)
    test(suite_decl.get('test_name', suite_name), suite_target)
  endif

  gentest_textual_suite_targets += {suite_name: suite_target}
  gentest_textual_suite_codegen_targets += {suite_name: suite_codegen}
endforeach
