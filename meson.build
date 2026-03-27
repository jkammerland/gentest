project('gentest', 'cpp', default_options: ['cpp_std=c++20'])

fmt_dep = dependency('fmt', required: false)
cpp = meson.get_compiler('cpp')
fs = import('fs')
py = import('python').find_installation('python3')

inc = include_directories('include', 'tests', 'third_party/include')
build_inc = include_directories('.')

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
  '-I@0@'.format(join_paths(meson.project_source_root(), 'include')),
  '-I@0@'.format(join_paths(meson.project_source_root(), 'tests')),
  '-I@0@'.format(join_paths(meson.project_source_root(), 'third_party', 'include')),
]
clang_arg_flags = []
foreach arg : clang_args
  clang_arg_flags += ['--clang-arg=@0@'.format(arg)]
endforeach

tests_suites = ['unit', 'integration', 'fixtures', 'skiponly']
buildsystem_codegen = files('scripts/gentest_buildsystem_codegen.py')
# The repo-local Meson integration keeps the explicit 2-step model:
# generate mocks first, then attach codegen to the consuming test surface.
# Textual targets use the helper's Meson backend directly. Named-module targets
# reuse the same backend-agnostic substrate, but Meson drives the module
# compilation itself because the native named-module path is not reliable here.
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

module_stem_script = '''
import hashlib
import re
import sys

stem = sys.argv[1]
sanitized = re.sub(r"[^A-Za-z0-9_]", "_", stem)
if not sanitized:
    sanitized = "tu"
if len(sanitized) > 24:
    sanitized = sanitized[:16] + "_" + hashlib.md5(sanitized.encode("utf-8")).hexdigest()[:8]
print(sanitized)
'''

module_metadata_compile_script = '''
import json
import pathlib
import subprocess
import sys

metadata_path = pathlib.Path(sys.argv[1])
module_name = sys.argv[2]
output_obj = sys.argv[3]
output_pcm = sys.argv[4]
metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
source_path = ""
for entry in metadata.get("module_sources", []):
    if entry.get("module_name") == module_name:
        source_path = entry.get("path", "")
        break
if source_path == "":
    raise SystemExit(f"module source not found in {metadata_path}: {module_name}")

command = sys.argv[5:] + ["-c", source_path, "-o", output_obj]
if output_pcm != "-":
    command.append(f"-fmodule-output={output_pcm}")
subprocess.run(command, check=True)
'''

module_codegen_backend = 'generic'
module_public_external_sources = [
  '--external-module-source', 'gentest=@0@'.format(join_paths(meson.project_source_root(), 'include', 'gentest', 'gentest.cppm')),
  '--external-module-source', 'gentest.bench_util=@0@'.format(join_paths(meson.project_source_root(), 'include', 'gentest', 'gentest.bench_util.cppm')),
  '--external-module-source', 'gentest.mock=@0@'.format(join_paths(meson.project_source_root(), 'include', 'gentest', 'gentest.mock.cppm')),
]
module_compile_args = [
  '-std=c++20',
  '-DFMT_HEADER_ONLY',
  '-I@0@'.format(join_paths(meson.project_source_root(), 'include')),
  '-I@0@'.format(join_paths(meson.project_source_root(), 'tests')),
  '-I@0@'.format(join_paths(meson.project_source_root(), 'third_party', 'include')),
  '-I@0@'.format(meson.current_build_dir()),
] + cpp.get_supported_arguments(['-Wno-unknown-attributes', '-Wno-attributes', '-Wno-unknown-warning-option'])
module_interface_compile_base = []
module_import_compile_base = []
module_compile_supported = false
module_obj_ext = 'o'
if host_machine.system() == 'windows'
  module_obj_ext = 'obj'
endif
if cpp.get_id() == 'clang'
  module_compile_supported = true
  module_interface_compile_base = cpp.cmd_array() + module_compile_args + ['-fprebuilt-module-path=@BUILD_ROOT@']
  module_import_compile_base = cpp.cmd_array() + module_compile_args + ['-fprebuilt-module-path=@BUILD_ROOT@']
elif cpp.get_id() == 'gcc'
  module_compile_supported = true
  module_interface_compile_base = cpp.cmd_array() + module_compile_args + ['-fmodules-ts']
  module_import_compile_base = cpp.cmd_array() + module_compile_args + ['-fmodules-ts']
endif

consumer_module_suite_stem = run_command(py, '-c', module_stem_script, 'suite_0000', check: true).stdout().strip()
consumer_module_wrapper_cpp = 'tu_0000_@0@.module.gentest.cppm'.format(consumer_module_suite_stem)
consumer_module_wrapper_h = 'tu_0000_@0@.gentest.h'.format(consumer_module_suite_stem)

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
      '--backend', 'meson',
      '--mode', 'suite',
      '--kind', 'textual',
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

consumer_mock_out_dir = meson.current_build_dir()
consumer_mock_defs_cpp = 'consumer_textual_mocks_defs.cpp'
consumer_mock_anchor_cpp = 'consumer_textual_mocks_anchor.cpp'
consumer_mock_codegen_h = 'tu_0000_consumer_textual_mocks_defs.gentest.h'
consumer_mock_registry_h = 'consumer_textual_mocks_mock_registry.hpp'
consumer_mock_impl_h = 'consumer_textual_mocks_mock_impl.hpp'
consumer_mock_domain_registry_h = 'consumer_textual_mocks_mock_registry__domain_0000_header.hpp'
consumer_mock_domain_impl_h = 'consumer_textual_mocks_mock_impl__domain_0000_header.hpp'
consumer_mock_staged_defs_h = 'def_0000_header_mock_defs.hpp'
consumer_mock_public_h = 'gentest_consumer_mocks.hpp'

consumer_mocks = custom_target(
  'gen_consumer_textual_mocks',
  output: [
    consumer_mock_defs_cpp,
    consumer_mock_anchor_cpp,
    consumer_mock_codegen_h,
    consumer_mock_registry_h,
    consumer_mock_impl_h,
    consumer_mock_domain_registry_h,
    consumer_mock_domain_impl_h,
    consumer_mock_staged_defs_h,
    consumer_mock_public_h,
  ],
  input: [
    'tests/consumer/header_mock_defs.hpp',
    'tests/consumer/service.hpp',
    buildsystem_codegen,
  ] + support_inputs,
  command: [
    py,
    buildsystem_codegen,
    '--backend', 'meson',
    '--mode', 'mocks',
    '--kind', 'textual',
    '--codegen', codegen,
    '--source-root', meson.project_source_root(),
    '--out-dir', consumer_mock_out_dir,
    '--wrapper-output', join_paths(consumer_mock_out_dir, consumer_mock_defs_cpp),
    '--anchor-output', join_paths(consumer_mock_out_dir, consumer_mock_anchor_cpp),
    '--header-output', join_paths(consumer_mock_out_dir, consumer_mock_codegen_h),
    '--public-header', join_paths(consumer_mock_out_dir, consumer_mock_public_h),
    '--mock-registry', join_paths(consumer_mock_out_dir, consumer_mock_registry_h),
    '--mock-impl', join_paths(consumer_mock_out_dir, consumer_mock_impl_h),
    '--target-id', 'consumer_textual_mocks',
    '--defs-file', '@INPUT0@',
    '--include-root', join_paths(meson.project_source_root(), 'include'),
    '--include-root', join_paths(meson.project_source_root(), 'tests'),
    '--include-root', join_paths(meson.project_source_root(), 'third_party', 'include'),
    '--compdb', meson.current_build_dir(),
    clang_arg_flags,
  ],
  build_by_default: true
)

consumer_mock_lib = static_library(
  'gentest_consumer_textual_mocks_meson',
  [consumer_mocks[0], consumer_mocks[1]],
  include_directories: [inc, build_inc],
  cpp_args: runtime_cargs,
  dependencies: runtime_deps,
  link_with: [libgentest_runtime],
)

consumer_wrapper_cpp = 'tu_0000_consumer_textual_cases.gentest.cpp'
consumer_wrapper_h = 'tu_0000_consumer_textual_cases.gentest.h'
consumer_clang_arg_flags = clang_arg_flags + ['--clang-arg=-I@0@'.format(consumer_mock_out_dir)]
consumer_codegen = custom_target(
  'gen_consumer_textual_cases',
  output: [consumer_wrapper_cpp, consumer_wrapper_h],
  input: ['tests/buildsystems/consumer_textual_cases.cpp', buildsystem_codegen, consumer_mocks] + support_inputs,
  command: [
    py,
    buildsystem_codegen,
    '--backend', 'meson',
    '--mode', 'suite',
    '--kind', 'textual',
    '--codegen', codegen,
    '--source-root', meson.project_source_root(),
    '--out-dir', meson.current_build_dir(),
    '--wrapper-output', join_paths(meson.current_build_dir(), consumer_wrapper_cpp),
    '--header-output', join_paths(meson.current_build_dir(), consumer_wrapper_h),
    '--source-file', '@INPUT0@',
    '--compdb', meson.current_build_dir(),
    consumer_clang_arg_flags,
  ],
  build_by_default: true
)

consumer_exe = executable(
  'gentest_consumer_textual_meson',
  [consumer_codegen[0], 'tests/consumer/main.cpp'],
  include_directories: [inc, build_inc],
  link_with: [libgentest_main, libgentest_runtime, consumer_mock_lib],
  cpp_args: runtime_cargs,
  dependencies: runtime_deps
)

test('meson_consumer_textual', consumer_exe)

if module_compile_supported
  consumer_module_mock_anchor_cpp = 'consumer_module_mocks_anchor.cpp'
  consumer_module_mock_registry_h = 'consumer_module_mocks_mock_registry.hpp'
  consumer_module_mock_impl_h = 'consumer_module_mocks_mock_impl.hpp'
  consumer_module_mock_metadata_json = 'consumer_module_mocks_mock_metadata.json'
  consumer_module_mocks = custom_target(
    'gen_consumer_module_mocks',
    output: [
      consumer_module_mock_anchor_cpp,
      consumer_module_mock_registry_h,
      consumer_module_mock_impl_h,
      consumer_module_mock_metadata_json,
    ],
    input: [
      'tests/consumer/service_module.cppm',
      'tests/consumer/module_mock_defs.cppm',
      buildsystem_codegen,
    ] + support_inputs,
    command: [
      py,
      buildsystem_codegen,
      '--backend', module_codegen_backend,
      '--mode', 'mocks',
      '--kind', 'modules',
      '--codegen', codegen,
      '--source-root', meson.project_source_root(),
      '--out-dir', meson.current_build_dir(),
      '--anchor-output', join_paths(meson.current_build_dir(), consumer_module_mock_anchor_cpp),
      '--mock-registry', join_paths(meson.current_build_dir(), consumer_module_mock_registry_h),
      '--mock-impl', join_paths(meson.current_build_dir(), consumer_module_mock_impl_h),
      '--target-id', 'consumer_module_mocks',
      '--metadata-output', join_paths(meson.current_build_dir(), consumer_module_mock_metadata_json),
      '--module-name', 'gentest.consumer_mocks',
      '--defs-file', '@INPUT0@',
      '--defs-file', '@INPUT1@',
      '--include-root', join_paths(meson.project_source_root(), 'include'),
      '--include-root', join_paths(meson.project_source_root(), 'tests'),
      '--include-root', join_paths(meson.project_source_root(), 'third_party', 'include'),
      '--compdb', meson.current_build_dir(),
      '--external-module-source', 'gentest.mock=@0@'.format(join_paths(meson.project_source_root(), 'include', 'gentest', 'gentest.mock.cppm')),
      clang_arg_flags,
    ],
    build_by_default: false,
  )
  consumer_module_mock_metadata = consumer_module_mocks[3]
  consumer_module_mock_anchor = consumer_module_mocks[0]
  consumer_module_suite_codegen = custom_target(
    'gen_consumer_module_cases',
    output: [consumer_module_wrapper_cpp, consumer_module_wrapper_h],
    input: ['tests/consumer/cases.cppm', buildsystem_codegen, consumer_module_mock_metadata] + support_inputs,
    command: [
      py,
      buildsystem_codegen,
      '--backend', module_codegen_backend,
      '--mode', 'suite',
      '--kind', 'modules',
      '--codegen', codegen,
      '--source-root', meson.project_source_root(),
      '--out-dir', meson.current_build_dir(),
      '--wrapper-output', join_paths(meson.current_build_dir(), consumer_module_wrapper_cpp),
      '--header-output', join_paths(meson.current_build_dir(), consumer_module_wrapper_h),
      '--source-file', '@INPUT0@',
      '--mock-metadata', '@INPUT2@',
      '--compdb', meson.current_build_dir(),
      '--include-root', join_paths(meson.project_source_root(), 'include'),
      '--include-root', join_paths(meson.project_source_root(), 'tests'),
      '--include-root', join_paths(meson.project_source_root(), 'third_party', 'include'),
      module_public_external_sources,
      clang_arg_flags,
    ],
    build_by_default: false,
  )

  if cpp.get_id() == 'clang'
    gentest_module = custom_target(
      'compile_gentest_module',
      output: ['gentest.@0@'.format(module_obj_ext), 'gentest.pcm'],
      input: ['include/gentest/gentest.cppm'] + support_inputs,
      command: module_interface_compile_base + ['-c', '@INPUT0@', '-o', '@OUTPUT0@', '-fmodule-output=@OUTPUT1@'],
      build_by_default: false,
    )
    gentest_bench_util_module = custom_target(
      'compile_gentest_bench_util_module',
      output: ['gentest.bench_util.@0@'.format(module_obj_ext), 'gentest.bench_util.pcm'],
      input: ['include/gentest/gentest.bench_util.cppm'] + support_inputs,
      command: module_interface_compile_base + ['-c', '@INPUT0@', '-o', '@OUTPUT0@', '-fmodule-output=@OUTPUT1@'],
      build_by_default: false,
    )
    gentest_mock_module = custom_target(
      'compile_gentest_mock_module',
      output: ['gentest.mock.@0@'.format(module_obj_ext), 'gentest.mock.pcm'],
      input: ['include/gentest/gentest.mock.cppm'] + support_inputs,
      command: module_interface_compile_base + ['-c', '@INPUT0@', '-o', '@OUTPUT0@', '-fmodule-output=@OUTPUT1@'],
      build_by_default: false,
    )
    consumer_service_module = custom_target(
      'compile_consumer_service_module',
      output: ['gentest.consumer_service.@0@'.format(module_obj_ext), 'gentest.consumer_service.pcm'],
      input: [consumer_module_mock_metadata],
      command: [
        py, '-c', module_metadata_compile_script,
        '@INPUT0@', 'gentest.consumer_service', '@OUTPUT0@', '@OUTPUT1@',
        module_interface_compile_base,
      ],
      build_by_default: false,
    )
    consumer_mock_defs_module = custom_target(
      'compile_consumer_mock_defs_module',
      output: ['gentest.consumer_mock_defs.@0@'.format(module_obj_ext), 'gentest.consumer_mock_defs.pcm'],
      input: [consumer_module_mock_metadata],
      depends: [consumer_service_module, gentest_mock_module],
      command: [
        py, '-c', module_metadata_compile_script,
        '@INPUT0@', 'gentest.consumer_mock_defs', '@OUTPUT0@', '@OUTPUT1@',
        module_interface_compile_base,
      ],
      build_by_default: false,
    )
    consumer_mocks_module = custom_target(
      'compile_consumer_mocks_module',
      output: ['gentest.consumer_mocks.@0@'.format(module_obj_ext), 'gentest.consumer_mocks.pcm'],
      input: [consumer_module_mock_metadata],
      depends: [consumer_service_module, consumer_mock_defs_module, gentest_mock_module],
      command: [
        py, '-c', module_metadata_compile_script,
        '@INPUT0@', 'gentest.consumer_mocks', '@OUTPUT0@', '@OUTPUT1@',
        module_interface_compile_base,
      ],
      build_by_default: false,
    )
    consumer_cases_module = custom_target(
      'compile_consumer_cases_module',
      output: ['gentest.consumer_cases.@0@'.format(module_obj_ext), 'gentest.consumer_cases.pcm'],
      input: [consumer_module_suite_codegen[0]],
      depends: [consumer_module_suite_codegen, consumer_mocks_module, gentest_module, gentest_bench_util_module, gentest_mock_module],
      command: module_interface_compile_base + ['-c', '@INPUT0@', '-o', '@OUTPUT0@', '-fmodule-output=@OUTPUT1@'],
      build_by_default: false,
    )
    consumer_module_main = custom_target(
      'compile_consumer_module_main',
      output: ['gentest.consumer_module_main.@0@'.format(module_obj_ext)],
      input: ['tests/consumer/main.cpp'],
      depends: [consumer_cases_module, consumer_mocks_module, gentest_module, gentest_bench_util_module, gentest_mock_module],
      command: module_import_compile_base + ['-DGENTEST_CONSUMER_USE_MODULES=1', '-c', '@INPUT0@', '-o', '@OUTPUT@'],
      build_by_default: false,
    )
  else
    gentest_module = custom_target(
      'compile_gentest_module',
      output: 'gentest.@0@'.format(module_obj_ext),
      input: ['include/gentest/gentest.cppm'] + support_inputs,
      command: module_interface_compile_base + ['-c', '@INPUT0@', '-o', '@OUTPUT@'],
      build_by_default: false,
    )
    gentest_bench_util_module = custom_target(
      'compile_gentest_bench_util_module',
      output: 'gentest.bench_util.@0@'.format(module_obj_ext),
      input: ['include/gentest/gentest.bench_util.cppm'] + support_inputs,
      command: module_interface_compile_base + ['-c', '@INPUT0@', '-o', '@OUTPUT@'],
      build_by_default: false,
    )
    gentest_mock_module = custom_target(
      'compile_gentest_mock_module',
      output: 'gentest.mock.@0@'.format(module_obj_ext),
      input: ['include/gentest/gentest.mock.cppm'] + support_inputs,
      command: module_interface_compile_base + ['-c', '@INPUT0@', '-o', '@OUTPUT@'],
      build_by_default: false,
    )
    consumer_service_module = custom_target(
      'compile_consumer_service_module',
      output: 'gentest.consumer_service.@0@'.format(module_obj_ext),
      input: [consumer_module_mock_metadata],
      command: [
        py, '-c', module_metadata_compile_script,
        '@INPUT0@', 'gentest.consumer_service', '@OUTPUT@', '-',
        module_interface_compile_base,
      ],
      build_by_default: false,
    )
    consumer_mock_defs_module = custom_target(
      'compile_consumer_mock_defs_module',
      output: 'gentest.consumer_mock_defs.@0@'.format(module_obj_ext),
      input: [consumer_module_mock_metadata],
      depends: [consumer_service_module, gentest_mock_module],
      command: [
        py, '-c', module_metadata_compile_script,
        '@INPUT0@', 'gentest.consumer_mock_defs', '@OUTPUT@', '-',
        module_interface_compile_base,
      ],
      build_by_default: false,
    )
    consumer_mocks_module = custom_target(
      'compile_consumer_mocks_module',
      output: 'gentest.consumer_mocks.@0@'.format(module_obj_ext),
      input: [consumer_module_mock_metadata],
      depends: [consumer_service_module, consumer_mock_defs_module, gentest_mock_module],
      command: [
        py, '-c', module_metadata_compile_script,
        '@INPUT0@', 'gentest.consumer_mocks', '@OUTPUT@', '-',
        module_interface_compile_base,
      ],
      build_by_default: false,
    )
    consumer_cases_module = custom_target(
      'compile_consumer_cases_module',
      output: 'gentest.consumer_cases.@0@'.format(module_obj_ext),
      input: [consumer_module_suite_codegen[0]],
      depends: [consumer_module_suite_codegen, consumer_mocks_module, gentest_module, gentest_bench_util_module, gentest_mock_module],
      command: module_interface_compile_base + ['-c', '@INPUT0@', '-o', '@OUTPUT@'],
      build_by_default: false,
    )
    consumer_module_main = custom_target(
      'compile_consumer_module_main',
      output: 'gentest.consumer_module_main.@0@'.format(module_obj_ext),
      input: ['tests/consumer/main.cpp'],
      depends: [consumer_cases_module, consumer_mocks_module, gentest_module, gentest_bench_util_module, gentest_mock_module],
      command: module_import_compile_base + ['-DGENTEST_CONSUMER_USE_MODULES=1', '-c', '@INPUT0@', '-o', '@OUTPUT@'],
      build_by_default: false,
    )
  endif

  executable(
    'gentest_consumer_module_meson',
    [consumer_module_mock_anchor],
    objects: [
      gentest_module[0],
      gentest_bench_util_module[0],
      gentest_mock_module[0],
      consumer_service_module[0],
      consumer_mock_defs_module[0],
      consumer_mocks_module[0],
      consumer_cases_module[0],
      consumer_module_main[0],
    ],
    include_directories: [inc, build_inc],
    link_with: [libgentest_main, libgentest_runtime],
    cpp_args: runtime_cargs,
    dependencies: runtime_deps,
    build_by_default: false,
  )
endif

# Convenience: run the CMake-based cross-compile POC from Meson.
bash = find_program('bash', required: false)
if bash.found()
  run_target(
    'poc_cross_aarch64_qemu',
    command: [bash, join_paths(meson.project_source_root(), 'scripts', 'poc_cross_aarch64_qemu.sh')]
  )
endif
